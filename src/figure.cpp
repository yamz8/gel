#include "figure.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QImageWriter>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QTextStream>
#include <cmath>

namespace Figure {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("Figure", text); }

constexpr double LabelPt = 8, SizePt = 7, AnnotationPt = 8, Margin = 3, Tick = 3;

QFont figureFont(double points, QPaintDevice *device) {
    QFont font;
    font.setFamilies({"Arial", "Helvetica", "Liberation Sans", "DejaVu Sans"});
    // Sizes are given in points of the figure; the painter scales points to
    // device pixels, so undo the device's own DPI here.
    font.setPointSizeF(points * 72.0 / device->logicalDpiY());
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QString number(double value, int decimals) { return QString::number(value, 'f', decimals); }

// Where everything sits, in points.
struct Layout {
    QRectF crop;              // frame pixels shown
    double k = 1;             // points per frame pixel
    QRectF gel;               // where the gel goes
    QSizeF page;
    bool rotatedLabels = false;
    struct Mark { double y; QString text; };
    QList<Mark> sizeMarks;
};

QList<Layout::Mark> ladderMarks(const Session &session, const Analysis::Results &results) {
    QList<Layout::Mark> marks;
    for (const auto &band : results.bands)
        if (band.ladder && std::isfinite(band.ladderSize))
            marks << Layout::Mark{band.position, Analysis::formatSize(band.ladderSize, session.ladder.unit)};
    return marks;
}

Layout layout(const Session &session, const Frame &frame, const Analysis::Results &results, const Options &options) {
    QImage probe(1, 1, QImage::Format_ARGB32);
    Layout l;
    l.crop = effectiveCrop(session, frame);
    const QFontMetricsF labelMetrics(figureFont(LabelPt, &probe), &probe);
    const QFontMetricsF sizeMetrics(figureFont(SizePt, &probe), &probe);

    for (const auto &mark : ladderMarks(session, results))
        if (mark.y >= l.crop.top() && mark.y <= l.crop.bottom())
            l.sizeMarks << mark;
    double left = Margin;
    for (const auto &mark : l.sizeMarks)
        left = std::max(left, Margin + sizeMetrics.horizontalAdvance(mark.text) + Tick + 2);

    const double width = options.widthMm / 25.4 * 72;
    const double gelWidth = std::max(10.0, width - left - Margin);
    l.k = gelWidth / l.crop.width();

    // Labels sit level when each fits over its lane, and turn 45 degrees
    // when any would run into its neighbor.
    double widest = 0;
    for (const auto &lane : session.lanes) {
        const double w = labelMetrics.horizontalAdvance(lane.label);
        widest = std::max(widest, w);
        if (w > lane.width * l.k * 1.2 + 2)
            l.rotatedLabels = true;
    }
    const double labelHeight = session.lanes.isEmpty() ? 0
        : l.rotatedLabels ? widest * M_SQRT1_2 + labelMetrics.height() * M_SQRT1_2 + 2
                          : labelMetrics.height() + 2;
    l.gel = QRectF(left, Margin + labelHeight, gelWidth, l.crop.height() * l.k);
    l.page = QSizeF(width, l.gel.bottom() + Margin);
    return l;
}

void drawArrow(QPainter &p, QPointF from, QPointF to, double width) {
    const QLineF line(from, to);
    if (line.length() < 1)
        return;
    const double head = std::min(line.length() * .6, width * 4.5);
    const QPointF unit = (to - from) / line.length();
    const QPointF normal(-unit.y(), unit.x());
    const QPointF base = to - unit * head;
    p.drawLine(from, base + unit * (head * .2));
    QPainterPath path;
    path.moveTo(to);
    path.lineTo(base + normal * head * .45);
    path.lineTo(base - normal * head * .45);
    path.closeSubpath();
    p.fillPath(path, p.pen().color());
}

void paint(QPainter &p, const Layout &l, const QImage &gel, const QColor &ink, const Session &session) {
    const double scale = p.device()->logicalDpiX() / 72.0;
    p.scale(scale, scale);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::LosslessImageRendering);
    p.fillRect(QRectF(QPointF(0, 0), l.page), Qt::white);
    p.drawImage(l.gel, gel);

    auto toPage = [&](QPointF frame) {
        return QPointF(l.gel.left() + (frame.x() - l.crop.left()) * l.k, l.gel.top() + (frame.y() - l.crop.top()) * l.k);
    };

    const QFont labelFont = figureFont(LabelPt, p.device());
    p.setFont(labelFont);
    p.setPen(Qt::black);
    const QFontMetricsF labelMetrics(labelFont, p.device());
    for (const auto &lane : session.lanes) {
        if (lane.center < l.crop.left() || lane.center > l.crop.right())
            continue;
        const double x = toPage({lane.center, 0}).x();
        if (l.rotatedLabels) {
            p.save();
            p.translate(x, l.gel.top() - 2);
            p.rotate(-45);
            p.drawText(QPointF(0, -labelMetrics.descent()), lane.label);
            p.restore();
        } else {
            const double w = labelMetrics.horizontalAdvance(lane.label);
            p.drawText(QPointF(x - w / 2, l.gel.top() - 2 - labelMetrics.descent()), lane.label);
        }
    }

    const QFont sizeFont = figureFont(SizePt, p.device());
    p.setFont(sizeFont);
    const QFontMetricsF sizeMetrics(sizeFont, p.device());
    p.setPen(QPen(Qt::black, .5));
    for (const auto &mark : l.sizeMarks) {
        const double y = toPage({0, mark.y}).y();
        p.drawLine(QPointF(l.gel.left() - Tick, y), QPointF(l.gel.left(), y));
        const double w = sizeMetrics.horizontalAdvance(mark.text);
        p.drawText(QPointF(l.gel.left() - Tick - 1.5 - w, y + (sizeMetrics.ascent() - sizeMetrics.descent()) / 2),
                   mark.text);
    }

    // Annotations stay inside the gel, so a crop clips them like the image.
    p.save();
    p.setClipRect(l.gel);
    const QFont annotationFont = figureFont(AnnotationPt, p.device());
    p.setFont(annotationFont);
    const QFontMetricsF annotationMetrics(annotationFont, p.device());
    p.setPen(QPen(ink, 1, Qt::SolidLine, Qt::RoundCap));
    for (const auto &a : session.annotations) {
        if (a.kind == Annotation::Arrow)
            drawArrow(p, toPage(a.from), toPage(a.to), 1);
        else
            p.drawText(toPage(a.from) + QPointF(0, annotationMetrics.ascent()), a.text);
    }
    p.restore();
}

QByteArray xmp(const QString &title, const QString &description) {
    auto escape = [](QString s) { return s.toHtmlEscaped().toUtf8(); };
    return "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
           "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
           "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
           "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"><xmp:CreatorTool>Gel " +
           QCoreApplication::applicationVersion().toUtf8() + "</xmp:CreatorTool><dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" +
           escape(title) + "</rdf:li></rdf:Alt></dc:title><dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">" +
           escape(description) + "</rdf:li></rdf:Alt></dc:description></rdf:Description></rdf:RDF></x:xmpmeta>\n"
           "<?xpacket end=\"w\"?>";
}

bool commit(QSaveFile &file, const QByteArray &bytes, QString *error) {
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        *error = tr("Could not write %1: %2").arg(QFileInfo(file.fileName()).fileName(), file.errorString());
        return false;
    }
    return true;
}
} // namespace

QImage displayImage(const Frame &frame, const ViewSettings &view, DisplayLevels levels) {
    return renderDisplay(frame.render(), frame.width(), frame.height(), levels, view.invert, view.brightness,
                         view.contrast);
}

QRectF effectiveCrop(const Session &session, const Frame &frame) {
    const QRectF crop = session.view.crop.isEmpty() ? frame.rect() : session.view.crop.intersected(frame.rect());
    return crop.isEmpty() ? frame.rect() : crop;
}

QColor annotationColor(const QImage &display, const QRectF &crop) {
    const QRect r = crop.toAlignedRect().intersected(display.rect());
    double sum = 0;
    long count = 0;
    const int step = std::max(1, std::max(r.width(), r.height()) / 200);
    for (int y = r.top(); y <= r.bottom(); y += step)
        for (int x = r.left(); x <= r.right(); x += step, ++count)
            sum += qGray(display.pixel(x, y));
    return count && sum / count < 128 ? Qt::white : Qt::black;
}

// Metadata stays in English so anyone reviewing the figure can read it,
// whatever language the app is in.
QString metadata(const Session &session, const GelImage &image, const Analysis::Results &results) {
    const auto &v = session.view;
    QStringList lines;
    lines << QString("Made with Gel %1 from %2 (%3-bit, SHA-256 %4).")
                 .arg(QCoreApplication::applicationVersion(), QFileInfo(image.path()).fileName())
                 .arg(image.bitDepth())
                 .arg(QString::fromLatin1(image.sha256()));
    QString crop = "none";
    if (!v.crop.isEmpty())
        crop = QString("x %1, y %2, %3 x %4 px").arg(number(v.crop.x(), 0), number(v.crop.y(), 0),
                                                      number(v.crop.width(), 0), number(v.crop.height(), 0));
    lines << QString("Display adjustments, which do not change the measurements: invert %1; brightness %2; "
                     "contrast %3; rotation %4 degrees plus %5 degrees; crop %6.")
                 .arg(v.invert ? "on" : "off", number(v.brightness, 2), number(v.contrast, 2))
                 .arg(v.quarterTurns * 90)
                 .arg(number(v.angle, 2), crop);
    const auto &q = session.quant;
    QString background = backgroundName(q.background);
    if (q.background == BackgroundMethod::RollingBall)
        background += QString(", radius %1 px").arg(number(q.ballRadius, 0));
    lines << QString("Quantification: %1 bands; background %2; normalization %3.")
                 .arg(results.dark ? "dark" : "bright", background, normalizationName(q.normalization));
    if (results.fit.ok)
        lines << QString("Ladder: %1 (%2), degree %3 fit of log10(size) on migration, R^2 %4.")
                     .arg(session.ladder.preset.isEmpty() ? "custom" : session.ladder.preset, session.ladder.unit)
                     .arg(results.fit.degree)
                     .arg(number(results.fit.r2, 4));
    return lines.join('\n');
}

QByteArray csv(const Session &session, const Analysis::Results &results) {
    auto quote = [](QString s) {
        if (s.contains(',') || s.contains('"') || s.contains('\n'))
            s = '"' + s.replace('"', "\"\"") + '"';
        return s;
    };
    auto value = [](double v, int decimals) { return std::isfinite(v) ? QString::number(v, 'f', decimals) : QString(); };
    QString out;
    QTextStream stream(&out);
    stream << "lane,label,band,position_px,size,size_unit,extrapolated,raw_volume,normalized_volume,"
              "saturated_pixels,ladder\n";
    for (const auto &b : results.bands) {
        stream << b.lane + 1 << ',' << quote(b.laneLabel) << ',' << b.band + 1 << ',' << value(b.position, 2) << ','
               << value(b.size, 1) << ',' << (std::isfinite(b.size) ? session.ladder.unit : QString()) << ','
               << (std::isfinite(b.size) ? (b.extrapolated ? "yes" : "no") : "") << ',' << value(b.volume, 1) << ','
               << value(b.normalized, 4) << ',' << b.saturatedPixels << ',' << (b.ladder ? "yes" : "no") << '\n';
    }
    stream.flush();
    return out.toUtf8();
}

bool exportTo(const QString &path, const QString &format, const GelImage &image, const Session &session,
              const Options &options, QString *error) {
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(image.path()).absoluteFilePath()) {
        *error = tr("Refusing to overwrite the original image.");
        return false;
    }
    if (options.dpi < 30 || options.dpi > 2400 || options.widthMm < 10 || options.widthMm > 1000) {
        *error = tr("The resolution must be 30 to 2400 dpi and the width 10 to 1000 mm.");
        return false;
    }
    const Frame frame(&image, session.view.quarterTurns, session.view.angle);
    const Analysis::Results results = Analysis::quantify(frame, session);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = tr("Could not write %1: %2").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    if (format == "csv")
        return commit(file, csv(session, results), error);

    const Layout l = layout(session, frame, results, options);
    const QImage display = displayImage(frame, session.view, autoLevels(image));
    const QImage cropped = display.copy(l.crop.toAlignedRect());
    const QColor ink = annotationColor(display, l.crop);
    // Resample once to the output resolution: averaging when shrinking, and
    // plain pixel repetition when enlarging, so no detail is invented.
    const QSize pixels(std::max(1, int(std::lround(l.gel.width() * options.dpi / 72))),
                       std::max(1, int(std::lround(l.gel.height() * options.dpi / 72))));
    const QImage gel = cropped.scaled(pixels, Qt::IgnoreAspectRatio,
                                      pixels.width() < cropped.width() ? Qt::SmoothTransformation : Qt::FastTransformation);
    const QString title = QFileInfo(image.path()).completeBaseName();
    const QString description = metadata(session, image, results);
    const QSize pagePixels(int(std::ceil(l.page.width() * options.dpi / 72)),
                           int(std::ceil(l.page.height() * options.dpi / 72)));

    if (format == "png") {
        QImage page(pagePixels, QImage::Format_RGB32);
        page.setDotsPerMeterX(int(std::lround(options.dpi / .0254)));
        page.setDotsPerMeterY(int(std::lround(options.dpi / .0254)));
        {
            QPainter painter(&page);
            paint(painter, l, gel, ink, session);
        }
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QImageWriter writer(&buffer, "png");
        writer.setText("Title", title);
        writer.setText("Software", "Gel " + QCoreApplication::applicationVersion());
        writer.setText("Description", description);
        if (!writer.write(page)) {
            *error = tr("Could not write %1: %2").arg(QFileInfo(path).fileName(), writer.errorString());
            return false;
        }
        return commit(file, buffer.data(), error);
    }
    if (format == "svg") {
        QBuffer buffer;
        QSvgGenerator generator;
        generator.setOutputDevice(&buffer);
        generator.setResolution(options.dpi);
        generator.setSize(pagePixels);
        generator.setViewBox(QRect(QPoint(0, 0), pagePixels));
        generator.setTitle(title);
        generator.setDescription(description);
        {
            QPainter painter(&generator);
            paint(painter, l, gel, ink, session);
        }
        return commit(file, buffer.data(), error);
    }
    if (format == "pdf") {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        {
            QPdfWriter writer(&buffer);
            writer.setResolution(options.dpi);
            writer.setPageSize(QPageSize(l.page, QPageSize::Point, {}, QPageSize::ExactMatch));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            writer.setTitle(title);
            writer.setCreator("Gel " + QCoreApplication::applicationVersion());
            writer.setDocumentXmpMetadata(xmp(title, description));
            QPainter painter(&writer);
            paint(painter, l, gel, ink, session);
        }
        return commit(file, buffer.data(), error);
    }
    file.cancelWriting();
    *error = tr("Unknown export format %1.").arg(format);
    return false;
}

} // namespace Figure
