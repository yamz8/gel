#include "session.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Session", text); }

QJsonArray toArray(const QList<double> &values) {
    QJsonArray array;
    for (double v : values)
        array.append(v);
    return array;
}

QList<double> toDoubles(const QJsonValue &value) {
    QList<double> out;
    for (const auto &v : value.toArray())
        if (v.isDouble())
            out << v.toDouble();
    return out;
}

// Positions are kept to a hundredth of a pixel, so files diff cleanly.
double round2(double v) { return std::round(v * 100) / 100; }

template <typename Enum> Enum pick(const QString &name, std::initializer_list<std::pair<const char *, Enum>> names,
                                   Enum fallback) {
    for (const auto &[key, value] : names)
        if (name == QLatin1String(key))
            return value;
    return fallback;
}
}

QList<LadderPreset> ladderPresets() {
    // The common markers, largest band first. Sizes are editable per session,
    // so a lot with different bands only needs its list changed.
    return {
        {"1 kb", "bp", {10000, 8000, 6000, 5000, 4000, 3000, 2000, 1500, 1000, 500}},
        {"100 bp", "bp", {1517, 1200, 1000, 900, 800, 700, 600, 500, 400, 300, 200, 100}},
        {"1 kb Plus", "bp", {15000, 10000, 8000, 7000, 6000, 5000, 4000, 3000, 2000, 1500, 1000, 700, 500, 400,
                             300, 200, 75}},
        {"Protein kDa", "kDa", {250, 150, 100, 75, 50, 37, 25, 20, 15, 10}},
    };
}

LadderPreset ladderPreset(const QString &name) {
    for (const auto &preset : ladderPresets())
        if (preset.name == name)
            return preset;
    return {};
}

QString polarityName(Polarity p) {
    return p == Polarity::Bright ? "bright" : p == Polarity::Dark ? "dark" : "auto";
}
QString backgroundName(BackgroundMethod m) {
    return m == BackgroundMethod::LaneEdges ? "lane-edges" : m == BackgroundMethod::None ? "none" : "rolling-ball";
}
QString normalizationName(Normalization n) {
    return n == Normalization::Lane ? "lane" : n == Normalization::Band ? "band" : "none";
}

QString Session::sessionPathFor(const QString &imagePath) {
    const QFileInfo info(imagePath);
    return info.absoluteDir().filePath(info.completeBaseName() + ".gel");
}

QJsonObject Session::toJson(const QString &sessionDirectory) const {
    QJsonObject view{{"invert", this->view.invert},
                     {"brightness", this->view.brightness},
                     {"contrast", this->view.contrast},
                     {"quarterTurns", this->view.quarterTurns},
                     {"angle", this->view.angle}};
    if (!this->view.crop.isEmpty())
        view["crop"] = QJsonArray{round2(this->view.crop.x()), round2(this->view.crop.y()),
                                  round2(this->view.crop.width()), round2(this->view.crop.height())};

    QJsonObject quant{{"polarity", polarityName(this->quant.polarity)},
                      {"background", backgroundName(this->quant.background)},
                      {"ballRadius", this->quant.ballRadius},
                      {"normalization", normalizationName(this->quant.normalization)},
                      {"referenceLane", this->quant.referenceLane},
                      {"referenceBand", this->quant.referenceBand}};

    QJsonArray lanes;
    for (const auto &lane : this->lanes) {
        QJsonArray bands;
        for (const auto &band : lane.bands) {
            QJsonObject b{{"position", round2(band.position)}, {"lower", round2(band.lower)},
                          {"upper", round2(band.upper)}};
            if (band.manual)
                b["manual"] = true;
            bands.append(b);
        }
        lanes.append(QJsonObject{{"label", lane.label},
                                 {"center", round2(lane.center)},
                                 {"width", round2(lane.width)},
                                 {"top", round2(lane.top)},
                                 {"bottom", round2(lane.bottom)},
                                 {"bands", bands}});
    }

    QJsonArray annotations;
    for (const auto &a : this->annotations) {
        QJsonObject o{{"kind", a.kind == Annotation::Arrow ? "arrow" : "text"},
                      {"x", round2(a.from.x())}, {"y", round2(a.from.y())}};
        if (a.kind == Annotation::Arrow) {
            o["toX"] = round2(a.to.x());
            o["toY"] = round2(a.to.y());
        } else {
            o["text"] = a.text;
        }
        annotations.append(o);
    }

    QString image = imagePath;
    if (!sessionDirectory.isEmpty() && !image.isEmpty())
        image = QDir(sessionDirectory).relativeFilePath(image);

    return {{"format", "gel"},
            {"version", 1},
            {"image", image},
            {"imageSha256", QString::fromLatin1(imageSha256)},
            {"view", view},
            {"quantification", quant},
            {"ladder", QJsonObject{{"lane", ladder.lane},
                                   {"preset", ladder.preset},
                                   {"unit", ladder.unit},
                                   {"sizes", toArray(ladder.sizes)}}},
            {"lanes", lanes},
            {"annotations", annotations}};
}

Session Session::fromJson(const QJsonObject &o, const QString &sessionDirectory, QString *error) {
    Session s;
    if (o.value("format").toString() != "gel") {
        *error = tr("This is not a gel session.");
        return s;
    }
    if (o.value("version").toInt() > 1) {
        *error = tr("This session was saved by a newer version of Gel.");
        return s;
    }
    const QString image = o.value("image").toString();
    s.imagePath = image.isEmpty() || sessionDirectory.isEmpty() ? image
                                                                : QDir(sessionDirectory).absoluteFilePath(image);
    s.imageSha256 = o.value("imageSha256").toString().toLatin1();

    const QJsonObject view = o.value("view").toObject();
    s.view.invert = view.value("invert").toBool();
    s.view.brightness = view.value("brightness").toDouble(0);
    s.view.contrast = view.value("contrast").toDouble(1);
    s.view.quarterTurns = view.value("quarterTurns").toInt(0);
    s.view.angle = view.value("angle").toDouble(0);
    const QList<double> crop = toDoubles(view.value("crop"));
    if (crop.size() == 4)
        s.view.crop = QRectF(crop[0], crop[1], crop[2], crop[3]);

    const QJsonObject quant = o.value("quantification").toObject();
    s.quant.polarity = pick<Polarity>(quant.value("polarity").toString(),
                                      {{"bright", Polarity::Bright}, {"dark", Polarity::Dark}}, Polarity::Auto);
    s.quant.background = pick<BackgroundMethod>(
        quant.value("background").toString(),
        {{"lane-edges", BackgroundMethod::LaneEdges}, {"none", BackgroundMethod::None}},
        BackgroundMethod::RollingBall);
    s.quant.ballRadius = quant.value("ballRadius").toDouble(30);
    s.quant.normalization = pick<Normalization>(quant.value("normalization").toString(),
                                                {{"lane", Normalization::Lane}, {"band", Normalization::Band}},
                                                Normalization::None);
    s.quant.referenceLane = quant.value("referenceLane").toInt(-1);
    s.quant.referenceBand = quant.value("referenceBand").toInt(-1);

    const QJsonObject ladder = o.value("ladder").toObject();
    s.ladder.lane = ladder.value("lane").toInt(-1);
    s.ladder.preset = ladder.value("preset").toString();
    s.ladder.unit = ladder.value("unit").toString("bp");
    s.ladder.sizes = toDoubles(ladder.value("sizes"));

    for (const auto &value : o.value("lanes").toArray()) {
        const QJsonObject l = value.toObject();
        Lane lane;
        lane.label = l.value("label").toString();
        lane.center = l.value("center").toDouble();
        lane.width = l.value("width").toDouble(20);
        lane.top = l.value("top").toDouble();
        lane.bottom = l.value("bottom").toDouble();
        for (const auto &bv : l.value("bands").toArray()) {
            const QJsonObject b = bv.toObject();
            lane.bands << Band{b.value("position").toDouble(), b.value("lower").toDouble(),
                               b.value("upper").toDouble(), b.value("manual").toBool()};
        }
        s.lanes << lane;
    }

    for (const auto &value : o.value("annotations").toArray()) {
        const QJsonObject a = value.toObject();
        Annotation annotation;
        annotation.kind = a.value("kind").toString() == "arrow" ? Annotation::Arrow : Annotation::Text;
        annotation.from = {a.value("x").toDouble(), a.value("y").toDouble()};
        annotation.to = {a.value("toX").toDouble(), a.value("toY").toDouble()};
        annotation.text = a.value("text").toString();
        s.annotations << annotation;
    }
    return s;
}

bool Session::save(const QString &path, QString *error) const {
    // QSaveFile writes beside the target and renames over it, so a crash
    // mid-save never leaves half a session.
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(toJson(QFileInfo(path).absolutePath())).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = tr("Could not save %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    return true;
}

Session Session::load(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (!document.isObject()) {
        *error = tr("%1 is not valid JSON: %2").arg(QFileInfo(path).fileName(), parse.errorString());
        return {};
    }
    return fromJson(document.object(), QFileInfo(path).absolutePath(), error);
}
