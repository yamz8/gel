#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

// Everything a .gel file holds: which image, how it is shown, where the lanes
// and bands are, and how they are measured. Measurements themselves are not
// stored; they are recomputed from the image every time, so they can't go
// stale. Positions are in analysis-frame pixels (see Frame).

struct ViewSettings {
    bool invert = false;
    double brightness = 0;   // -1..1, added after the levels
    double contrast = 1;     // 0.1..5, around the middle gray
    int quarterTurns = 0;    // clockwise
    double angle = 0;        // degrees, clockwise, small straightening turns
    QRectF crop;             // empty means the whole frame

    bool operator==(const ViewSettings &) const = default;
};

struct Band {
    double position = 0;     // the peak, frame y
    double lower = 0;        // integration bounds, frame y, inclusive
    double upper = 0;
    bool manual = false;     // picked by hand, so auto-detect keeps it
};

struct Lane {
    QString label;
    double center = 0;
    double width = 20;
    double top = 0;
    double bottom = 0;
    QList<Band> bands;
};

struct Ladder {
    int lane = -1;           // -1: no ladder
    QString preset;
    QList<double> sizes;     // largest first, in unit
    QString unit = "bp";
};

struct LadderPreset {
    QString name;
    QString unit;
    QList<double> sizes;
};
QList<LadderPreset> ladderPresets();
LadderPreset ladderPreset(const QString &name);

struct Annotation {
    enum Kind { Arrow, Text };
    Kind kind = Text;
    QPointF from;            // a text's anchor, or an arrow's tail
    QPointF to;              // an arrow's head
    QString text;
};

enum class Polarity { Auto, Bright, Dark };
enum class BackgroundMethod { RollingBall, LaneEdges, None };
enum class Normalization { None, Lane, Band };

struct Quantification {
    Polarity polarity = Polarity::Auto;
    BackgroundMethod background = BackgroundMethod::RollingBall;
    double ballRadius = 30;  // pixels
    Normalization normalization = Normalization::None;
    int referenceLane = -1;
    int referenceBand = -1;  // index within referenceLane, for Normalization::Band
};

struct Session {
    QString imagePath;       // absolute in memory, relative to the .gel on disk
    QByteArray imageSha256;
    ViewSettings view;
    Quantification quant;
    QList<Lane> lanes;
    Ladder ladder;
    QList<Annotation> annotations;

    QJsonObject toJson(const QString &sessionDirectory = {}) const;
    static Session fromJson(const QJsonObject &object, const QString &sessionDirectory, QString *error);
    bool save(const QString &path, QString *error) const;
    static Session load(const QString &path, QString *error);
    static QString sessionPathFor(const QString &imagePath);
};

QString polarityName(Polarity polarity);
QString backgroundName(BackgroundMethod method);
QString normalizationName(Normalization normalization);
