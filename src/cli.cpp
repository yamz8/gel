// The command structure follows Hype's (MIT, David Heinemeier Hansson).
#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <cstdio>

#include "analysis.h"
#include "figure.h"
#include "gelimage.h"
#include "session.h"

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}
QJsonValue number(double value) { return std::isfinite(value) ? QJsonValue(value) : QJsonValue(); }

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description, const QString &argument, const QString &help) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, "", name);
        if (!argument.isEmpty())
            parser.addPositionalArgument(argument, help);
    }
    QString argument(int i) const { return parser.positionalArguments().value(i); }
};

// A .gel session with its image, or an image alone, which gets the session
// beside it when there is one and fresh detection otherwise.
struct Loaded {
    GelImage image;
    Session session;
    QString sessionPath;
    bool detected = false;
    QStringList warnings;
};

void detect(Loaded &loaded) {
    const Frame frame(&loaded.image, loaded.session.view.quarterTurns, loaded.session.view.angle);
    const bool dark = Analysis::resolveDark(loaded.image, loaded.session.quant.polarity);
    if (loaded.session.lanes.isEmpty())
        loaded.session.lanes = Analysis::detectLanes(frame, dark, loaded.session.view.crop);
    for (auto &lane : loaded.session.lanes) {
        QList<Band> bands;
        for (const auto &band : lane.bands)
            if (band.manual)
                bands << band;
        if (bands.size() == lane.bands.size() && !bands.isEmpty())
            continue;
        for (const auto &band : Analysis::detectBands(Analysis::laneProfile(frame, lane, dark, loaded.session.quant)))
            bands << band;
        std::sort(bands.begin(), bands.end(), [](const Band &a, const Band &b) { return a.position < b.position; });
        lane.bands = bands;
    }
    loaded.detected = true;
}

bool load(const QString &path, Loaded &loaded, QString *error) {
    if (path.isEmpty()) {
        *error = "Give an image or a .gel session.";
        return false;
    }
    if (!QFileInfo::exists(path)) {
        *error = "No such file: " + path;
        return false;
    }
    const bool isSession = QFileInfo(path).suffix().toLower() == "gel";
    QString sessionPath = isSession ? path : Session::sessionPathFor(path);
    if (isSession || QFileInfo::exists(sessionPath)) {
        loaded.session = Session::load(sessionPath, error);
        if (!error->isEmpty())
            return false;
        loaded.sessionPath = QFileInfo(sessionPath).absoluteFilePath();
        if (!isSession)
            loaded.session.imagePath = QFileInfo(path).absoluteFilePath();
    } else {
        loaded.session.imagePath = QFileInfo(path).absoluteFilePath();
    }
    if (!loaded.image.load(loaded.session.imagePath, error))
        return false;
    if (!loaded.session.imageSha256.isEmpty() && loaded.session.imageSha256 != loaded.image.sha256())
        loaded.warnings << "The image has changed since this session was saved.";
    loaded.session.imageSha256 = loaded.image.sha256();
    return true;
}

int saveIfAsked(const QCommandLineParser &parser, Loaded &loaded, bool json) {
    if (!parser.isSet("save"))
        return 0;
    const QString path = loaded.sessionPath.isEmpty() ? Session::sessionPathFor(loaded.image.path()) : loaded.sessionPath;
    QString error;
    if (!loaded.session.save(path, &error))
        return fail(error);
    if (!json)
        print(stdout, "Saved " + path);
    return 0;
}

QJsonObject laneJson(const Lane &lane, int index) {
    return {{"lane", index + 1}, {"label", lane.label}, {"center", lane.center}, {"width", lane.width},
            {"top", lane.top}, {"bottom", lane.bottom}, {"bands", int(lane.bands.size())}};
}

int lanes(const QStringList &arguments) {
    Command command("lanes", "Detect the lanes of a gel or blot image and print them.", "image",
                    "PNG, JPEG, or TIFF image");
    command.parser.addOption({"json", "Print JSON"});
    command.parser.addOption({"save", "Write the lanes and bands to the .gel session beside the image"});
    command.parser.process(arguments);
    Loaded loaded;
    QString error;
    if (!load(command.argument(1), loaded, &error))
        return fail(error);
    loaded.session.lanes.clear();
    detect(loaded);
    const bool json = command.parser.isSet("json");
    const Frame frame(&loaded.image, loaded.session.view.quarterTurns, loaded.session.view.angle);
    const bool dark = Analysis::resolveDark(loaded.image, loaded.session.quant.polarity);
    if (json) {
        QJsonArray array;
        for (int i = 0; i < loaded.session.lanes.size(); ++i)
            array.append(laneJson(loaded.session.lanes[i], i));
        print(QJsonObject{{"image", loaded.image.path()}, {"width", frame.width()}, {"height", frame.height()},
                          {"bitDepth", loaded.image.bitDepth()}, {"bands", dark ? "dark" : "bright"}, {"lanes", array}});
    } else {
        print(stdout, QString("%1: %2 x %3, %4-bit, %5 bands, %6 lanes")
                          .arg(QFileInfo(loaded.image.path()).fileName())
                          .arg(frame.width()).arg(frame.height()).arg(loaded.image.bitDepth())
                          .arg(dark ? "dark" : "bright").arg(loaded.session.lanes.size()));
        for (int i = 0; i < loaded.session.lanes.size(); ++i) {
            const auto &l = loaded.session.lanes[i];
            print(stdout, QString("  lane %1  center %2  width %3  rows %4-%5  %6 bands")
                              .arg(i + 1).arg(l.center, 0, 'f', 1).arg(l.width, 0, 'f', 1)
                              .arg(l.top, 0, 'f', 0).arg(l.bottom, 0, 'f', 0).arg(l.bands.size()));
        }
    }
    return saveIfAsked(command.parser, loaded, json);
}

int quant(const QStringList &arguments) {
    Command command("quant", "Quantify every band: size from the ladder, background-subtracted volume, and "
                             "normalized volume. An image without a session gets lanes and bands detected.",
                    "session", ".gel session, or an image");
    command.parser.addOption({"json", "Print JSON"});
    command.parser.addOption({"save", "Write detected lanes and bands to the .gel session"});
    command.parser.process(arguments);
    Loaded loaded;
    QString error;
    if (!load(command.argument(1), loaded, &error))
        return fail(error);
    bool needsBands = loaded.session.lanes.isEmpty();
    for (const auto &lane : loaded.session.lanes)
        needsBands = needsBands || lane.bands.isEmpty();
    if (needsBands)
        detect(loaded);
    const Frame frame(&loaded.image, loaded.session.view.quarterTurns, loaded.session.view.angle);
    const auto results = Analysis::quantify(frame, loaded.session);
    const QStringList warnings = loaded.warnings + results.warnings;
    const QString unit = loaded.session.ladder.unit;
    const bool json = command.parser.isSet("json");
    if (json) {
        QJsonArray bands;
        for (const auto &b : results.bands)
            bands.append(QJsonObject{{"lane", b.lane + 1}, {"label", b.laneLabel}, {"band", b.band + 1},
                                     {"position", b.position}, {"size", number(b.size)},
                                     {"unit", std::isfinite(b.size) ? QJsonValue(unit) : QJsonValue()},
                                     {"extrapolated", b.extrapolated}, {"volume", b.volume},
                                     {"normalized", number(b.normalized)}, {"saturatedPixels", b.saturatedPixels},
                                     {"ladder", b.ladder}});
        QJsonObject ladder{{"fitted", results.fit.ok}};
        if (results.fit.ok) {
            ladder["degree"] = results.fit.degree;
            ladder["r2"] = results.fit.r2;
        }
        print(QJsonObject{{"image", loaded.image.path()},
                          {"bands", results.dark ? "dark" : "bright"},
                          {"background", backgroundName(loaded.session.quant.background)},
                          {"normalization", normalizationName(loaded.session.quant.normalization)},
                          {"detected", loaded.detected},
                          {"ladder", ladder},
                          {"results", bands},
                          {"warnings", QJsonArray::fromStringList(warnings)}});
    } else {
        print(stdout, "lane\tlabel\tband\tsize\tvolume\tnormalized");
        for (const auto &b : results.bands)
            print(stdout, QString("%1\t%2\t%3\t%4\t%5\t%6")
                              .arg(b.lane + 1).arg(b.laneLabel).arg(b.band + 1)
                              .arg(std::isfinite(b.size) ? Analysis::formatSize(b.size, unit) + (b.extrapolated ? "*" : "") : "-")
                              .arg(b.volume, 0, 'f', 0)
                              .arg(std::isfinite(b.normalized) ? QString::number(b.normalized, 'f', 3) : "-"));
        for (const auto &w : warnings)
            print(stderr, "warning: " + w);
    }
    return saveIfAsked(command.parser, loaded, json);
}

int exportFile(const QStringList &arguments) {
    Command command("export", "Export the publication figure (PNG, SVG, or PDF) or the results (CSV).", "session",
                    ".gel session, or an image");
    command.parser.addOption({"png", "Figure as PNG", "file"});
    command.parser.addOption({"svg", "Figure as SVG", "file"});
    command.parser.addOption({"pdf", "Figure as PDF", "file"});
    command.parser.addOption({"csv", "Results as CSV", "file"});
    command.parser.addOption({"dpi", "Resolution of the figure (default 300)", "N", "300"});
    command.parser.addOption({"width-mm", "Width of the figure in millimeters (default 85, one column)", "mm", "85"});
    command.parser.addOption({"json", "Print JSON"});
    command.parser.process(arguments);
    Loaded loaded;
    QString error;
    if (!load(command.argument(1), loaded, &error))
        return fail(error);
    bool needsBands = loaded.session.lanes.isEmpty();
    for (const auto &lane : loaded.session.lanes)
        needsBands = needsBands || lane.bands.isEmpty();
    if (needsBands)
        detect(loaded);
    bool ok = false;
    Figure::Options options;
    options.dpi = command.parser.value("dpi").toInt(&ok);
    if (!ok)
        return fail("--dpi takes a whole number.");
    options.widthMm = command.parser.value("width-mm").toDouble(&ok);
    if (!ok)
        return fail("--width-mm takes a number.");
    QJsonArray written;
    int formats = 0;
    for (const char *format : {"png", "svg", "pdf", "csv"}) {
        if (!command.parser.isSet(format))
            continue;
        ++formats;
        const QString path = command.parser.value(format);
        if (!Figure::exportTo(path, format, loaded.image, loaded.session, options, &error))
            return fail(error);
        written.append(QFileInfo(path).absoluteFilePath());
        if (!command.parser.isSet("json"))
            print(stdout, "Wrote " + QFileInfo(path).absoluteFilePath());
    }
    if (formats == 0)
        return fail("Choose an output: --png, --svg, --pdf, or --csv <file>.");
    if (command.parser.isSet("json"))
        print(QJsonObject{{"written", written}});
    return 0;
}

int check(const QStringList &arguments) {
    Command command("check", "Report every problem with a session: a missing or changed image, lanes or bands "
                             "outside the image, a ladder that doesn't fit, saturated bands.",
                    "session", ".gel session");
    command.parser.addOption({"json", "Print JSON"});
    command.parser.process(arguments);
    struct Problem { bool error; QString message; };
    QList<Problem> problems;
    Loaded loaded;
    QString error;
    const QString path = command.argument(1);
    if (!load(path, loaded, &error)) {
        problems << Problem{true, error};
    } else {
        for (const auto &w : loaded.warnings)
            problems << Problem{false, w};
        const Frame frame(&loaded.image, loaded.session.view.quarterTurns, loaded.session.view.angle);
        const QRectF bounds = frame.rect().adjusted(-.5, -.5, .5, .5);
        const auto &s = loaded.session;
        if (s.lanes.isEmpty())
            problems << Problem{false, "There are no lanes."};
        for (int i = 0; i < s.lanes.size(); ++i) {
            const auto &l = s.lanes[i];
            const QString name = QString("Lane %1 (%2)").arg(i + 1).arg(l.label);
            if (l.width < 2)
                problems << Problem{true, name + " is narrower than 2 pixels."};
            if (l.center - l.width / 2 < bounds.left() || l.center + l.width / 2 > bounds.right() ||
                l.top < bounds.top() || l.bottom > bounds.bottom() || l.bottom <= l.top)
                problems << Problem{true, name + " reaches outside the image."};
            for (int b = 0; b < l.bands.size(); ++b) {
                const auto &band = l.bands[b];
                if (band.position < l.top || band.position > l.bottom || band.lower > band.upper ||
                    band.lower < l.top - .5 || band.upper > l.bottom + .5)
                    problems << Problem{true, QString("%1, band %2 lies outside its lane.").arg(name).arg(b + 1)};
            }
        }
        if (s.ladder.lane >= s.lanes.size())
            problems << Problem{true, QString("The ladder lane %1 doesn't exist.").arg(s.ladder.lane + 1)};
        for (double size : s.ladder.sizes)
            if (!(size > 0))
                problems << Problem{true, "Ladder sizes must be positive."};
        if (s.quant.ballRadius < 1)
            problems << Problem{true, "The rolling-ball radius must be at least 1 pixel."};
        const bool fatal = std::any_of(problems.begin(), problems.end(), [](const Problem &p) { return p.error; });
        if (!fatal) {
            const auto results = Analysis::quantify(frame, s);
            for (const auto &w : results.warnings)
                problems << Problem{false, w};
            if (s.ladder.lane >= 0 && !results.fit.ok && results.fit.message.isEmpty())
                problems << Problem{false, "The ladder could not be fitted."};
        }
    }
    const bool failed = std::any_of(problems.begin(), problems.end(), [](const Problem &p) { return p.error; });
    if (command.parser.isSet("json")) {
        QJsonArray array;
        for (const auto &p : problems)
            array.append(QJsonObject{{"severity", p.error ? "error" : "warning"}, {"message", p.message}});
        print(QJsonObject{{"session", path}, {"ok", !failed}, {"problems", array}});
    } else {
        for (const auto &p : problems)
            print(p.error ? stderr : stdout, QString("%1: %2: %3").arg(path, p.error ? "error" : "warning", p.message));
        if (problems.isEmpty())
            print(stdout, path + ": ok");
    }
    return failed ? 1 : 0;
}

// As Hype does: one shared copy, linked into Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.",
                    "install", "Copy to ~/.agents/skills/gel and link into ~/.claude/skills");
    command.parser.process(arguments);
    QFile bundled(":/skill/SKILL.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.argument(1).isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.argument(1) != "install")
        return fail("Use gel skill to print the skill, or gel skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/gel");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/gel");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/gel", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: gel <command> [options]\n\n" + cliSummary());
    return arguments.value(1).isEmpty() || arguments.value(1) == "help" ? 0 : 1;
}
} // namespace

QString cliSummary() {
    return "Window:\n"
           "  open [image|session]            Open the window\n\n"
           "Commands that need no display (gel help <command> for options):\n"
           "  lanes <image>                   Detect lanes and print them\n"
           "  quant <session|image>           Sizes, volumes, and normalized volumes of every band\n"
           "  export <session> --png|--svg|--pdf|--csv <out> [--dpi N]\n"
           "                                  Export the figure or the results\n"
           "  check <session>                 Report every problem with a session\n"
           "  skill [install]                 Print the skill for coding agents, or install it\n\n"
           "Each command takes --json, and exits 1 with the reason on stderr when it fails.";
}

bool isCliCommand(const QString &word) {
    return QStringList{"lanes", "quant", "export", "check", "skill", "help"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "lanes"    ? lanes(arguments)
           : command == "quant"  ? quant(arguments)
           : command == "export" ? exportFile(arguments)
           : command == "check"  ? check(arguments)
           : command == "skill"  ? skill(arguments)
                                 : help(arguments);
}
