#include "gelimage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <cmath>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("GelImage", text); }
}

QByteArray GelImage::fileSha256(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result().toHex();
}

bool GelImage::load(const QString &path, QString *error) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if ((suffix == "tif" || suffix == "tiff") && !QImageReader::supportedImageFormats().contains("tiff")) {
        *error = tr("TIFF support is missing. Install qt6-imageformats.");
        return false;
    }
    // Scanner TIFFs are routinely larger than Qt's default 256 MB guard.
    QImageReader::setAllocationLimit(2048);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        *error = tr("Could not read %1: %2").arg(QFileInfo(path).fileName(), reader.errorString());
        return false;
    }

    const int w = image.width(), h = image.height();
    std::vector<float> values(size_t(w) * h);
    int depth = 8;
    const auto format = image.format();
    if (format == QImage::Format_Grayscale16) {
        depth = 16;
        for (int y = 0; y < h; ++y) {
            const auto *line = reinterpret_cast<const quint16 *>(image.constScanLine(y));
            std::copy(line, line + w, values.begin() + size_t(y) * w);
        }
    } else if (format == QImage::Format_RGBA64 || format == QImage::Format_RGBX64 ||
               format == QImage::Format_RGBA64_Premultiplied) {
        // 16-bit color: luminance in 16 bits, so a gray image saved as RGB
        // keeps its exact values.
        depth = 16;
        for (int y = 0; y < h; ++y) {
            const auto *line = reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgba64 c = line[x];
                values[size_t(y) * w + x] = c.red() == c.green() && c.green() == c.blue()
                    ? c.red() : float(std::round(.2126 * c.red() + .7152 * c.green() + .0722 * c.blue()));
            }
        }
    } else if (format == QImage::Format_Grayscale8) {
        for (int y = 0; y < h; ++y) {
            const uchar *line = image.constScanLine(y);
            std::copy(line, line + w, values.begin() + size_t(y) * w);
        }
    } else {
        const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < h; ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb c = line[x];
                values[size_t(y) * w + x] = qRed(c) == qGreen(c) && qGreen(c) == qBlue(c)
                    ? qRed(c) : float(std::round(.2126 * qRed(c) + .7152 * qGreen(c) + .0722 * qBlue(c)));
            }
        }
    }

    *this = fromValues(w, h, depth, std::move(values));
    m_path = QFileInfo(path).absoluteFilePath();
    m_sha256 = fileSha256(path);
    return true;
}

GelImage GelImage::fromValues(int width, int height, int bitDepth, std::vector<float> values) {
    GelImage image;
    image.m_width = width;
    image.m_height = height;
    image.m_bitDepth = bitDepth;
    image.m_values = std::move(values);
    return image;
}

double GelImage::sample(double x, double y) const {
    x = std::clamp(x, 0.0, double(m_width - 1));
    y = std::clamp(y, 0.0, double(m_height - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(x0 + 1, m_width - 1), y1 = std::min(y0 + 1, m_height - 1);
    const double fx = x - x0, fy = y - y0;
    const double top = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
    const double bottom = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
    return top * (1 - fy) + bottom * fy;
}

Frame::Frame(const GelImage *image, int quarterTurns, double angleDegrees)
    : m_image(image), m_turns(((quarterTurns % 4) + 4) % 4) {
    const double radians = angleDegrees * M_PI / 180.0;
    m_cos = std::cos(radians);
    m_sin = std::sin(radians);
    const bool swapped = m_turns % 2 == 1;
    m_width = swapped ? image->height() : image->width();
    m_height = swapped ? image->width() : image->height();
}

QPointF Frame::toRaw(QPointF point) const {
    // Undo the free angle around the frame's center (a clockwise turn on
    // screen, where y points down), then the quarter turns.
    const double cx = (m_width - 1) / 2.0, cy = (m_height - 1) / 2.0;
    const double dx = point.x() - cx, dy = point.y() - cy;
    const double u = cx + m_cos * dx + m_sin * dy;
    const double v = cy - m_sin * dx + m_cos * dy;
    const int W = m_image->width(), H = m_image->height();
    switch (m_turns) {
    case 1: return {v, H - 1 - u};
    case 2: return {W - 1 - u, H - 1 - v};
    case 3: return {W - 1 - v, u};
    default: return {u, v};
    }
}

double Frame::sample(double x, double y) const {
    const QPointF raw = toRaw({x, y});
    return m_image->sample(raw.x(), raw.y());
}

std::vector<float> Frame::render() const {
    std::vector<float> out(size_t(m_width) * m_height);
    const bool straight = m_sin == 0 && m_cos == 1;
    for (int y = 0; y < m_height; ++y)
        for (int x = 0; x < m_width; ++x) {
            float value;
            if (straight) {
                // Quarter turns land on pixel centers, so copy exactly.
                const QPointF raw = toRaw({double(x), double(y)});
                value = m_image->at(int(std::lround(raw.x())), int(std::lround(raw.y())));
            } else {
                value = float(sample(x, y));
            }
            out[size_t(y) * m_width + x] = value;
        }
    return out;
}

DisplayLevels autoLevels(const GelImage &image) {
    if (image.bitDepth() == 8 || image.values().empty())
        return {0, 255};
    // A 16-bit scan rarely uses the whole range; stretch between the 0.1st
    // and 99.9th percentiles so it is visible at all.
    std::vector<float> sorted;
    const size_t step = std::max<size_t>(1, image.values().size() / 400000);
    for (size_t i = 0; i < image.values().size(); i += step)
        sorted.push_back(image.values()[i]);
    std::sort(sorted.begin(), sorted.end());
    const double low = sorted[size_t(.001 * (sorted.size() - 1))];
    const double high = sorted[size_t(.999 * (sorted.size() - 1))];
    return {low, high > low ? high : low + 1};
}

QImage renderDisplay(const std::vector<float> &values, int width, int height, DisplayLevels levels, bool invert,
                     double brightness, double contrast) {
    QImage image(width, height, QImage::Format_Grayscale8);
    const double span = levels.high - levels.low;
    for (int y = 0; y < height; ++y) {
        uchar *line = image.scanLine(y);
        const float *row = values.data() + size_t(y) * width;
        for (int x = 0; x < width; ++x) {
            double v = (row[x] - levels.low) / span;
            v = (v - .5) * contrast + .5 + brightness;
            v = std::clamp(v, 0.0, 1.0);
            if (invert)
                v = 1 - v;
            line[x] = uchar(std::lround(v * 255));
        }
    }
    return image;
}
