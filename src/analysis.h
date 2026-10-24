#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <vector>

#include "gelimage.h"
#include "session.h"

// The measurements. Every function reads raw values through a Frame and
// none of them looks at the display settings, so inverting, brightening, or
// cropping the view can't change a number.
namespace Analysis {

// Which way the signal points: bright bands on a dark gel (stained DNA,
// fluorescence) or dark bands on a light one (film, colorimetric blots).
bool darkBands(const GelImage &image);
bool resolveDark(const GelImage &image, Polarity polarity);
// Signal grows with the amount of material either way.
inline double signal(double raw, double maxValue, bool dark) { return dark ? maxValue - raw : raw; }

struct Profile {
    double top = 0;               // frame y of sample 0; sample i is at top + i
    std::vector<double> raw;      // lane-averaged signal per row
    std::vector<double> background;
    std::vector<double> net;      // raw - background
    std::vector<int> saturated;   // saturated pixels per row
};

Profile laneProfile(const Frame &frame, const Lane &lane, bool dark, const Quantification &quant);
std::vector<double> rollingBall(const std::vector<double> &profile, double radius);
std::vector<double> smooth(const std::vector<double> &values, double sigma);

// Lanes in region (frame coordinates; empty for the whole frame).
QList<Lane> detectLanes(const Frame &frame, bool dark, QRectF region = {});
QList<Band> detectBands(const Profile &profile);
// The band nearest y, snapped to the local peak, for a click in band mode.
Band bandAt(const Profile &profile, double y);

struct LadderFit {
    bool ok = false;
    int degree = 0;
    std::vector<double> coefficients;   // log10(size) = sum c[i] * t^i, t normalized position
    double offset = 0, scale = 1;       // t = (y - offset) / scale
    double r2 = 0;
    double minPosition = 0, maxPosition = 0;
    QList<double> positions, sizes;     // the points fitted, top to bottom
    QString message;
};
LadderFit fitLadder(const QList<double> &positions, const QList<double> &sizes);
double estimateSize(const LadderFit &fit, double position);

struct BandResult {
    int lane = 0, band = 0;             // zero-based
    QString laneLabel;
    double position = 0;
    double size = qQNaN();
    bool extrapolated = false;
    double volume = 0;
    double normalized = qQNaN();
    int saturatedPixels = 0;
    bool ladder = false;
    double ladderSize = qQNaN();        // the marker size assigned, ladder bands only
};

struct Results {
    bool dark = false;
    QList<BandResult> bands;
    LadderFit fit;
    QStringList warnings;
    QList<Profile> profiles;            // per lane

    QList<BandResult> forLane(int lane) const;
};

Results quantify(const Frame &frame, const Session &session);

// Sizes in the ladder's unit, as 1.5 kb, 750 bp, or 37 kDa.
QString formatSize(double size, const QString &unit);

} // namespace Analysis
