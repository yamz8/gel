#pragma once

#include <QByteArray>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <vector>

// One grayscale gel or blot image, as raw values. A 16-bit file keeps its
// 0-65535 values; an 8-bit or color file becomes 0-255 luminance. Nothing
// here ever writes the file back.
class GelImage {
public:
    bool load(const QString &path, QString *error);
    static GelImage fromValues(int width, int height, int bitDepth, std::vector<float> values);

    bool isNull() const { return m_width == 0 || m_height == 0; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    int bitDepth() const { return m_bitDepth; }
    // The largest value the file format can hold: a pixel there is saturated.
    double maxValue() const { return m_bitDepth == 16 ? 65535.0 : 255.0; }
    QString path() const { return m_path; }
    QByteArray sha256() const { return m_sha256; }

    float at(int x, int y) const { return m_values[size_t(y) * m_width + x]; }
    // Bilinear between pixel centers, clamped to the edge.
    double sample(double x, double y) const;
    const std::vector<float> &values() const { return m_values; }

    static QByteArray fileSha256(const QString &path);

private:
    int m_width = 0;
    int m_height = 0;
    int m_bitDepth = 8;
    std::vector<float> m_values;
    QString m_path;
    QByteArray m_sha256;
};

// The analysis frame: the raw image turned by quarter turns (clockwise) and
// then by a small free angle around its center. Lanes, bands, crops, and
// annotations all live in frame coordinates, and every measurement samples
// the raw values through this mapping, so turning the view never resamples
// or alters the data itself.
class Frame {
public:
    Frame() = default;
    Frame(const GelImage *image, int quarterTurns, double angleDegrees);

    int width() const { return m_width; }
    int height() const { return m_height; }
    QRectF rect() const { return QRectF(0, 0, m_width, m_height); }
    QPointF toRaw(QPointF point) const;
    double sample(double x, double y) const;
    // Every frame pixel's raw value, row by row.
    std::vector<float> render() const;
    const GelImage *image() const { return m_image; }

private:
    const GelImage *m_image = nullptr;
    int m_turns = 0;
    double m_cos = 1, m_sin = 0;
    int m_width = 0, m_height = 0;
};

// How the image is shown. None of it changes a measurement.
struct DisplayLevels {
    double low = 0, high = 255;
};
DisplayLevels autoLevels(const GelImage &image);
QImage renderDisplay(const std::vector<float> &frameValues, int width, int height, DisplayLevels levels,
                     bool invert, double brightness, double contrast);
