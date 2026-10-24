#pragma once

#include <QImage>
#include <QString>

#include "analysis.h"
#include "gelimage.h"
#include "session.h"

class QPainter;

// The publication figure: the gel as shown (cropped, turned, with its display
// levels), lane labels above it, ladder sizes beside it, and the annotations.
// Overlays used for editing (lane boxes, band marks) are left out.
namespace Figure {

struct Options {
    double widthMm = 85;     // a single journal column
    int dpi = 300;
};

// What the window shows: the frame with the display settings, uncropped.
QImage displayImage(const Frame &frame, const ViewSettings &view, DisplayLevels levels);
// Text annotations and arrows read on whichever the gel's background is.
QColor annotationColor(const QImage &display, const QRectF &crop);
QRectF effectiveCrop(const Session &session, const Frame &frame);
// How large a frame pixel comes out in the figure, so the window can show
// annotations at the size they will print.
double pointsPerPixel(const Session &session, const Frame &frame, const Analysis::Results &results,
                      const Options &options);

QString metadata(const Session &session, const GelImage &image, const Analysis::Results &results);
QByteArray csv(const Session &session, const Analysis::Results &results);

// format is png, svg, pdf, or csv.
bool exportTo(const QString &path, const QString &format, const GelImage &image, const Session &session,
              const Options &options, QString *error);

} // namespace Figure
