#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "analysis.h"
#include "figure.h"
#include "gelimage.h"
#include "session.h"
#include "synthetic.h"
#include "theme.h"
#include "translator.h"

namespace {
// Six lanes, a 1 kb ladder in the first, and bands of known size and amount.
Synthetic standardGel() {
    Synthetic gel;
    gel.width = 420;
    gel.height = 400;
    gel.noise = 25;
    gel.slopeY = 2;
    SyntheticLane ladder{40, 31, {}};
    for (double size : ladderPreset("1 kb").sizes)
        ladder.bands << SyntheticBand{Synthetic::migration(size), 9000, 3};
    gel.lanes << ladder;
    const QList<double> sizes{7000, 2500, 750};
    for (int i = 1; i < 6; ++i) {
        SyntheticLane lane{40.0 + 66 * i, 31, {}};
        for (int b = 0; b < sizes.size(); ++b)
            lane.bands << SyntheticBand{Synthetic::migration(sizes[b]), 3000.0 * i * (b + 1) / 2, 4};
        gel.lanes << lane;
    }
    return gel;
}

// The session a person would end up with: lanes and bands detected, the
// first lane a 1 kb ladder.
Session detected(const GelImage &image, const Frame &frame) {
    Session session;
    session.imagePath = image.path();
    session.lanes = Analysis::detectLanes(frame, Analysis::darkBands(image));
    for (auto &lane : session.lanes)
        lane.bands = Analysis::detectBands(Analysis::laneProfile(frame, lane, Analysis::darkBands(image), session.quant));
    session.ladder.lane = 0;
    session.ladder.preset = "1 kb";
    session.ladder.sizes = ladderPreset("1 kb").sizes;
    return session;
}

void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

QList<double> volumes(const Analysis::Results &results) {
    QList<double> out;
    for (const auto &b : results.bands)
        out << b.volume;
    return out;
}
} // namespace

class GelTests : public QObject {
    Q_OBJECT

private slots:
    void readsSixteenBitPngAndTiffExactly() {
        QVERIFY2(QImageReader::supportedImageFormats().contains("tiff"), "qt6-imageformats provides TIFF");
        QTemporaryDir dir;
        QImage source(4, 3, QImage::Format_Grayscale16);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 4; ++x)
                reinterpret_cast<quint16 *>(source.scanLine(y))[x] = quint16(x * 20000 + y * 1234 + 7);
        reinterpret_cast<quint16 *>(source.scanLine(2))[3] = 65535;
        for (const char *format : {"png", "tiff"}) {
            const QString path = dir.filePath(QString("sixteen.%1").arg(format));
            QVERIFY(QImageWriter(path, format).write(source));
            GelImage image;
            QString error;
            QVERIFY2(image.load(path, &error), qPrintable(error));
            QCOMPARE(image.bitDepth(), 16);
            QCOMPARE(image.at(1, 2), float(20000 + 2468 + 7));
            QCOMPARE(image.at(3, 2), 65535.0f);
            QCOMPARE(image.maxValue(), 65535.0);
        }
        const QString jpeg = dir.filePath("eight.jpg");
        QVERIFY(source.convertToFormat(QImage::Format_Grayscale8).save(jpeg));
        GelImage image;
        QString error;
        QVERIFY(image.load(jpeg, &error));
        QCOMPARE(image.bitDepth(), 8);
    }

    void reportsAnUnreadableFile() {
        QTemporaryDir dir;
        write(dir.filePath("broken.png"), "not an image");
        GelImage image;
        QString error;
        QVERIFY(!image.load(dir.filePath("broken.png"), &error));
        QVERIFY(error.contains("broken.png"));
    }

    void detectsLanesAtTheirCenters() {
        const Synthetic gel = standardGel();
        const GelImage image = gel.image();
        const Frame frame(&image, 0, 0);
        const auto lanes = Analysis::detectLanes(frame, false);
        QCOMPARE(lanes.size(), gel.lanes.size());
        for (int i = 0; i < lanes.size(); ++i) {
            QVERIFY2(std::abs(lanes[i].center - gel.lanes[i].center) <= 1.0,
                     qPrintable(QString("lane %1 at %2").arg(i).arg(lanes[i].center)));
            QVERIFY2(std::abs(lanes[i].width - 31) <= 3, qPrintable(QString::number(lanes[i].width)));
            QCOMPARE(lanes[i].label, QString::number(i + 1));
        }
    }

    void detectsBandsAtTheirPeaks() {
        const Synthetic gel = standardGel();
        const GelImage image = gel.image();
        const Frame frame(&image, 0, 0);
        const auto lanes = Analysis::detectLanes(frame, false);
        for (int l = 0; l < lanes.size(); ++l) {
            const auto bands = Analysis::detectBands(Analysis::laneProfile(frame, lanes[l], false, {}));
            QCOMPARE(bands.size(), gel.lanes[l].bands.size());
            for (int b = 0; b < bands.size(); ++b)
                QVERIFY2(std::abs(bands[b].position - gel.lanes[l].bands[b].y) <= 1.0,
                         qPrintable(QString("lane %1 band %2 at %3, expected %4")
                                        .arg(l).arg(b).arg(bands[b].position).arg(gel.lanes[l].bands[b].y)));
        }
    }

    void clickingSnapsToThePeak() {
        const Synthetic gel = standardGel();
        const GelImage image = gel.image();
        const Frame frame(&image, 0, 0);
        const Lane lane = Analysis::detectLanes(frame, false).at(2);
        const auto profile = Analysis::laneProfile(frame, lane, false, {});
        const double expected = gel.lanes[2].bands[1].y;
        const Band band = Analysis::bandAt(profile, expected + 3);
        QVERIFY(band.manual);
        QVERIFY(std::abs(band.position - expected) <= 1.0);
        QVERIFY(band.lower < expected && band.upper > expected);
    }

    void ladderEstimatesSizesWithinFivePercent() {
        const Synthetic gel = standardGel();
        const GelImage image = gel.image();
        const Frame frame(&image, 0, 0);
        const Session session = detected(image, frame);
        const auto results = Analysis::quantify(frame, session);
        QVERIFY2(results.fit.ok, qPrintable(results.fit.message));
        QVERIFY(results.fit.r2 > .999);
        const QList<double> truth{7000, 2500, 750};
        for (int l = 1; l < 6; ++l) {
            const auto bands = results.forLane(l);
            QCOMPARE(bands.size(), 3);
            for (int b = 0; b < 3; ++b) {
                const double error = std::abs(bands[b].size - truth[b]) / truth[b];
                QVERIFY2(error < .05, qPrintable(QString("lane %1 band %2: %3 for %4")
                                                     .arg(l).arg(b).arg(bands[b].size).arg(truth[b])));
                QVERIFY(!bands[b].extrapolated);
            }
        }
        const auto ladder = results.forLane(0);
        QCOMPARE(ladder.first().ladderSize, 10000.0);
        QCOMPARE(ladder.last().ladderSize, 500.0);
    }

    void ladderFitNeedsTwoPoints() {
        QVERIFY(!Analysis::fitLadder({100}, {1000}).ok);
        const auto fit = Analysis::fitLadder({100, 200, 300}, {3000, 1000});
        QVERIFY(fit.ok);
        QVERIFY(fit.message.contains("3"));
        QCOMPARE(fit.degree, 1);
        QVERIFY(std::abs(Analysis::estimateSize(fit, 150) - std::sqrt(3000.0 * 1000)) < 1);
    }

    void rollingBallRecoversVolumeOnASlope() {
        Synthetic gel;
        gel.width = 120;
        gel.height = 300;
        gel.background = 5000;
        gel.slopeY = 20;   // the background climbs 6000 over the lane
        gel.noise = 20;
        gel.lanes << SyntheticLane{60, 31, {{100, 4000, 4}, {190, 1500, 5}}};
        const GelImage image = gel.image();
        const Frame frame(&image, 0, 0);
        Session session;
        session.lanes << Lane{"1", 60, 31, 0, 299, {}};
        session.quant.ballRadius = 30;
        session.lanes[0].bands = Analysis::detectBands(Analysis::laneProfile(frame, session.lanes[0], false, session.quant));
        QCOMPARE(session.lanes[0].bands.size(), 2);
        const auto results = Analysis::quantify(frame, session);
        for (int b = 0; b < 2; ++b) {
            const double expected = Synthetic::volume(gel.lanes[0].bands[b], 31);
            const double error = std::abs(results.bands[b].volume - expected) / expected;
            QVERIFY2(error < .05, qPrintable(QString("band %1: %2 for %3").arg(b).arg(results.bands[b].volume).arg(expected)));
        }
    }

    void rollingBallLeavesALineAlone() {
        std::vector<double> line;
        for (int i = 0; i < 200; ++i)
            line.push_back(100 + 3.5 * i);
        const auto background = Analysis::rollingBall(line, 25);
        for (int i = 0; i < 200; ++i)
            QVERIFY(std::abs(background[i] - line[i]) < 1e-6);
    }

    void laneEdgesFollowABackgroundAcrossAndDown() {
        Synthetic gel;
        gel.width = 160;
        gel.height = 260;
        gel.background = 8000;
        gel.slopeX = 15;
        gel.slopeY = -10;
        gel.noise = 15;
        gel.lanes << SyntheticLane{80, 31, {{130, 5000, 4}}};
        const GelImage image = gel.image();
        const Frame frame(&image, 0, 0);
        Session session;
        session.quant.background = BackgroundMethod::LaneEdges;
        session.lanes << Lane{"1", 80, 31, 0, 259, {}};
        session.lanes[0].bands = Analysis::detectBands(Analysis::laneProfile(frame, session.lanes[0], false, session.quant));
        QCOMPARE(session.lanes[0].bands.size(), 1);
        const double expected = Synthetic::volume(gel.lanes[0].bands[0], 31);
        const double volume = Analysis::quantify(frame, session).bands[0].volume;
        QVERIFY2(std::abs(volume - expected) / expected < .05, qPrintable(QString("%1 for %2").arg(volume).arg(expected)));
    }

    void normalizesToAReferenceBandOrLane() {
        const Synthetic gel = standardGel();
        const GelImage image = gel.image();
        const Frame frame(&image, 0, 0);
        Session session = detected(image, frame);
        session.quant.normalization = Normalization::Band;
        session.quant.referenceLane = 1;
        session.quant.referenceBand = 0;
        auto results = Analysis::quantify(frame, session);
        // Amplitudes were 1500 * lane * (band + 1), all with the same sigma.
        for (const auto &b : results.bands) {
            if (b.lane == 0)
                continue;
            const double expected = double(b.lane * (b.band + 1));
            QVERIFY2(std::abs(b.normalized - expected) / expected < .05,
                     qPrintable(QString("lane %1 band %2: %3 for %4").arg(b.lane).arg(b.band).arg(b.normalized).arg(expected)));
        }

        session.quant.normalization = Normalization::Lane;
        results = Analysis::quantify(frame, session);
        // Lane 1 holds 1 + 2 + 3 = 6 parts.
        for (const auto &b : results.bands)
            if (b.lane > 0) {
                const double expected = b.lane * (b.band + 1) / 6.0;
                QVERIFY(std::abs(b.normalized - expected) / expected < .05);
            }

        session.quant.referenceLane = 99;
        results = Analysis::quantify(frame, session);
        QVERIFY(std::isnan(results.bands.first().normalized));
        QVERIFY(!results.warnings.isEmpty());
    }

    void sessionSurvivesAJsonRoundTrip() {
        QTemporaryDir dir;
        Session session;
        session.imagePath = dir.filePath("blot.tif");
        session.imageSha256 = "abc123";
        session.view = {true, .25, 1.5, 3, -1.25, QRectF(10, 20, 300, 200)};
        session.quant = {Polarity::Dark, BackgroundMethod::LaneEdges, 42, Normalization::Band, 2, 1};
        session.ladder = {0, "Protein kDa", {250, 150, 100}, "kDa"};
        session.lanes << Lane{"Ladder", 40, 30, 5, 395, {{60.5, 55, 66, false}}}
                      << Lane{"WT, 2h", 100.25, 29.5, 5, 395, {{120, 110, 130, true}}};
        session.annotations << Annotation{Annotation::Arrow, {1, 2}, {30, 40}, {}}
                            << Annotation{Annotation::Text, {50, 60}, {}, "β-actin"};

        const QString path = dir.filePath("blot.gel");
        QString error;
        QVERIFY2(session.save(path, &error), qPrintable(error));
        const Session loaded = Session::load(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.toJson(), session.toJson());
        QCOMPARE(loaded.imagePath, session.imagePath);

        // On disk the image is relative, so the folder can move.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("image").toString(), QString("blot.tif"));

        write(dir.filePath("other.gel"), R"({"format":"something"})");
        Session::load(dir.filePath("other.gel"), &error);
        QVERIFY(!error.isEmpty());
    }

    void leavesTheSourceImageUnchanged() {
        QTemporaryDir dir;
        const QString path = dir.filePath("gel.tif");
        QVERIFY(standardGel().qimage().save(path, "tiff"));
        const QByteArray before = GelImage::fileSha256(path);
        const QDateTime modified = QFileInfo(path).lastModified();

        GelImage image;
        QString error;
        QVERIFY(image.load(path, &error));
        QCOMPARE(image.sha256(), before);
        Frame frame(&image, 0, 0);
        Session session = detected(image, frame);
        session.view = {true, .3, 2, 0, 0, QRectF(5, 5, 400, 380)};
        session.annotations << Annotation{Annotation::Arrow, {200, 100}, {230, 130}, {}};
        QVERIFY(session.save(Session::sessionPathFor(path), &error));
        for (const char *format : {"png", "svg", "pdf", "csv"})
            QVERIFY2(Figure::exportTo(dir.filePath(QString("figure.%1").arg(format)), format, image, session, {}, &error),
                     qPrintable(error));
        QVERIFY(!Figure::exportTo(path, "png", image, session, {}, &error));

        QCOMPARE(GelImage::fileSha256(path), before);
        QCOMPARE(QFileInfo(path).lastModified(), modified);
        QVERIFY(QFile::exists(dir.filePath("gel.gel")));
    }

    void displayAdjustmentsDoNotChangeTheNumbers() {
        const Synthetic gel = standardGel();
        const GelImage image = gel.image();
        const Frame frame(&image, 0, 0);
        Session session = detected(image, frame);
        const auto plain = volumes(Analysis::quantify(frame, session));
        QVERIFY(!plain.isEmpty());
        session.view.invert = true;
        session.view.brightness = .4;
        session.view.contrast = 3;
        session.view.crop = QRectF(20, 30, 200, 100);
        QCOMPARE(volumes(Analysis::quantify(frame, session)), plain);

        // The display image does change, so the test above means something.
        const QImage a = Figure::displayImage(frame, {}, autoLevels(image));
        const QImage b = Figure::displayImage(frame, session.view, autoLevels(image));
        QVERIFY(a != b);
    }

    void turningTheViewFollowsTheData() {
        // The same gel scanned sideways, then turned back a quarter in the app.
        const Synthetic gel = standardGel();
        const auto upright = gel.values();
        std::vector<float> sideways(upright.size());
        const int W = gel.width, H = gel.height;   // sideways is H wide, W tall
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < H; ++x)
                sideways[size_t(y) * H + x] = upright[size_t(x) * W + (W - 1 - y)];
        const GelImage a = gel.image();
        const GelImage b = GelImage::fromValues(H, W, 16, sideways);
        const Frame fa(&a, 0, 0), fb(&b, 1, 0);
        QCOMPARE(fb.width(), W);
        QCOMPARE(fb.height(), H);
        const auto ra = Analysis::quantify(fa, detected(a, fa));
        const auto rb = Analysis::quantify(fb, detected(b, fb));
        QCOMPARE(rb.bands.size(), ra.bands.size());
        for (int i = 0; i < ra.bands.size(); ++i) {
            QVERIFY(std::abs(rb.bands[i].position - ra.bands[i].position) < .01);
            QVERIFY(std::abs(rb.bands[i].volume - ra.bands[i].volume) / ra.bands[i].volume < 1e-6);
        }
        // Four quarter turns are no turn at all.
        const Frame full(&a, 4, 0);
        QCOMPARE(full.toRaw({12.5, 34}), QPointF(12.5, 34));
    }

    void freeAngleTurnsAroundTheCenter() {
        const GelImage image = GelImage::fromValues(101, 51, 8, std::vector<float>(101 * 51, 0));
        const Frame frame(&image, 0, 90);
        const QPointF center = frame.toRaw({50, 25});
        QVERIFY(std::abs(center.x() - 50) < 1e-9 && std::abs(center.y() - 25) < 1e-9);
        // Turned 90 degrees clockwise, the point right of center came from above it.
        const QPointF right = frame.toRaw({60, 25});
        QVERIFY(std::abs(right.x() - 50) < 1e-9 && std::abs(right.y() - 15) < 1e-9);
    }

    void darkBandsMeasureLikeBrightOnes() {
        Synthetic gel = standardGel();
        const GelImage bright = gel.image();
        gel.background = 4000;
        gel.dark = true;
        gel.slopeY = -2;
        const GelImage dark = gel.image();
        QVERIFY(!Analysis::darkBands(bright));
        QVERIFY(Analysis::darkBands(dark));
        const Frame fb(&bright, 0, 0), fd(&dark, 0, 0);
        const auto rb = Analysis::quantify(fb, detected(bright, fb));
        const auto rd = Analysis::quantify(fd, detected(dark, fd));
        QVERIFY(rd.dark);
        QCOMPARE(rd.bands.size(), rb.bands.size());
        for (int i = 0; i < rb.bands.size(); ++i)
            QVERIFY2(std::abs(rd.bands[i].volume - rb.bands[i].volume) / rb.bands[i].volume < .05,
                     qPrintable(QString("%1 vs %2").arg(rd.bands[i].volume).arg(rb.bands[i].volume)));
    }

    void flagsSaturatedBands() {
        Synthetic gel;
        gel.width = 100;
        gel.height = 200;
        gel.lanes << SyntheticLane{50, 31, {{60, 90000, 4}, {140, 5000, 4}}};
        const GelImage image = gel.image();
        const Frame frame(&image, 0, 0);
        Session session;
        session.lanes << Lane{"1", 50, 31, 0, 199, {}};
        session.lanes[0].bands = Analysis::detectBands(Analysis::laneProfile(frame, session.lanes[0], false, session.quant));
        const auto results = Analysis::quantify(frame, session);
        QCOMPARE(results.bands.size(), 2);
        QVERIFY(results.bands[0].saturatedPixels > 0);
        QCOMPARE(results.bands[1].saturatedPixels, 0);
        QVERIFY(results.warnings.join(' ').contains("saturated"));
    }

    void exportsAFigureWithItsSettings() {
        QTemporaryDir dir;
        const Synthetic gel = standardGel();
        const QString path = dir.filePath("gel.png");
        QVERIFY(gel.qimage().save(path));
        GelImage image;
        QString error;
        QVERIFY(image.load(path, &error));
        const Frame frame(&image, 0, 0);
        Session session = detected(image, frame);
        session.view.invert = true;
        session.lanes[0].label = "Ladder";

        QVERIFY(Figure::exportTo(dir.filePath("figure.png"), "png", image, session, {85, 300}, &error));
        QImageReader reader(dir.filePath("figure.png"));
        const QString description = reader.text("Description");
        QVERIFY(description.contains("invert on"));
        QVERIFY(description.contains("rolling-ball"));
        const QImage figure = reader.read();
        QVERIFY(std::abs(figure.width() - 85 / 25.4 * 300) <= 2);
        QVERIFY(std::abs(figure.dotsPerMeterX() * .0254 - 300) < 1);

        QVERIFY(Figure::exportTo(dir.filePath("figure.svg"), "svg", image, session, {85, 600}, &error));
        QFile svg(dir.filePath("figure.svg"));
        QVERIFY(svg.open(QIODevice::ReadOnly));
        const QByteArray svgText = svg.readAll();
        QVERIFY(svgText.contains("<svg"));
        QVERIFY(svgText.contains("invert on"));
        QVERIFY(svgText.contains("Ladder"));

        QVERIFY(Figure::exportTo(dir.filePath("figure.pdf"), "pdf", image, session, {85, 300}, &error));
        QFile pdf(dir.filePath("figure.pdf"));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        const QByteArray pdfBytes = pdf.readAll();
        QVERIFY(pdfBytes.startsWith("%PDF"));
        QVERIFY(pdfBytes.contains("invert on"));

        QVERIFY(Figure::exportTo(dir.filePath("results.csv"), "csv", image, session, {}, &error));
        QFile csv(dir.filePath("results.csv"));
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(csv.readAll()).trimmed().split('\n');
        QCOMPARE(lines.size(), 1 + 10 + 5 * 3);
        QVERIFY(lines[0].startsWith("lane,label,band,position_px,size"));
        QVERIFY(lines[1].startsWith("1,Ladder,1,"));

        QVERIFY(!Figure::exportTo(dir.filePath("x.png"), "png", image, session, {85, 5}, &error));
    }

    void formatsSizes() {
        QLocale::setDefault(QLocale::c());
        QCOMPARE(Analysis::formatSize(1500, "bp"), QString("1.5 kb"));
        QCOMPARE(Analysis::formatSize(10000, "bp"), QString("10 kb"));
        QCOMPARE(Analysis::formatSize(752.4, "bp"), QString("752 bp"));
        QCOMPARE(Analysis::formatSize(37.25, "kDa"), QString("37.3 kDa"));
        QVERIFY(Analysis::formatSize(qQNaN(), "bp").isEmpty());
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
    }

    void catalogsCoverEveryVisibleString() {
        // Every qsTr() in the QML and tr() in the C++ the window shows (the
        // command line stays English) has a translation in every catalog.
        QStringList keys;
        const QDir sources(SOURCE_DIR);
        for (const QString &name : sources.entryList({"*.qml", "*.cpp"})) {
            if (name == "cli.cpp")
                continue;
            QFile file(sources.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(file.readAll());
            // A call with a literal: tr("…") in C++, qsTr("…") in QML.
            const QString string = QStringLiteral("\\(\"((?:[^\"\\\\]|\\\\.)*)\"");
            const QRegularExpression call((name.endsWith(".qml") ? QStringLiteral("qsTr") : QStringLiteral("\\btr")) + string);
            for (auto it = call.globalMatch(text); it.hasNext();)
                keys << it.next().captured(1);
        }
        keys.removeDuplicates();
        QVERIFY(keys.size() > 100);
        for (const QString code : {"da", "de", "es"}) {
            QFile file(":/i18n/" + code + ".json");
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(code));
            QJsonParseError parse;
            const QJsonObject catalog = QJsonDocument::fromJson(file.readAll(), &parse).object();
            QVERIFY2(parse.error == QJsonParseError::NoError, qPrintable(code + ": " + parse.errorString()));
            for (const QString &key : keys)
                QVERIFY2(catalog.contains(key), qPrintable(code + " lacks \"" + key + "\""));
        }
        // English carries the plural forms of counted strings.
        QFile english(":/i18n/en.json");
        QVERIFY(english.open(QIODevice::ReadOnly));
        const QJsonObject en = QJsonDocument::fromJson(english.readAll()).object();
        for (const QString &key : keys)
            if (key.contains("%n"))
                QVERIFY2(en.value(key).isObject(), qPrintable("en lacks plurals for " + key));
    }
};

QTEST_MAIN(GelTests)
#include "gel_tests.moc"
