#pragma once

// Synthetic gels with known answers: lanes as hard-edged strips, bands as
// Gaussians down each lane, on a background that can slope both ways, with
// seeded noise. A band's true volume is amplitude * sigma * sqrt(2 pi) * the
// lane's width in pixels.

#include <QImage>
#include <QList>
#include <QRandomGenerator>
#include <cmath>

#include "gelimage.h"

struct SyntheticBand {
    double y;
    double amplitude;
    double sigma = 4;
};

struct SyntheticLane {
    double center;
    int width = 31;   // odd, so a lane covers whole pixels around its center
    QList<SyntheticBand> bands;
};

struct Synthetic {
    int width = 420;
    int height = 320;
    int bitDepth = 16;
    double background = 2000;
    double slopeX = 0;     // per pixel
    double slopeY = 0;
    double noise = 0;      // Gaussian sigma, raw units
    bool dark = false;     // dark bands on a light background, like film
    quint32 seed = 7;
    QList<SyntheticLane> lanes;

    double maxValue() const { return bitDepth == 16 ? 65535 : 255; }

    static double volume(const SyntheticBand &band, int laneWidth) {
        return band.amplitude * band.sigma * std::sqrt(2 * M_PI) * laneWidth;
    }

    std::vector<float> values() const {
        std::vector<float> out(size_t(width) * height);
        QRandomGenerator random(seed);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                double v = background + slopeX * x + slopeY * y;
                for (const auto &lane : lanes) {
                    if (std::abs(x - lane.center) > (lane.width - 1) / 2.0 + 1e-9)
                        continue;
                    for (const auto &band : lane.bands)
                        v += band.amplitude * std::exp(-.5 * std::pow((y - band.y) / band.sigma, 2));
                }
                if (noise > 0) {
                    // Box-Muller from the seeded generator, so runs repeat.
                    const double u1 = std::max(1e-12, random.generateDouble()), u2 = random.generateDouble();
                    v += noise * std::sqrt(-2 * std::log(u1)) * std::cos(2 * M_PI * u2);
                }
                if (dark)
                    v = maxValue() - v;
                out[size_t(y) * width + x] = float(std::clamp(std::round(v), 0.0, maxValue()));
            }
        return out;
    }

    GelImage image() const { return GelImage::fromValues(width, height, bitDepth, values()); }

    QImage qimage() const {
        const auto v = values();
        QImage image(width, height, bitDepth == 16 ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8);
        for (int y = 0; y < height; ++y) {
            if (bitDepth == 16) {
                auto *line = reinterpret_cast<quint16 *>(image.scanLine(y));
                for (int x = 0; x < width; ++x)
                    line[x] = quint16(v[size_t(y) * width + x]);
            } else {
                uchar *line = image.scanLine(y);
                for (int x = 0; x < width; ++x)
                    line[x] = uchar(v[size_t(y) * width + x]);
            }
        }
        return image;
    }

    // Where a ladder band of this size runs: log-linear with a gentle bend,
    // like a real agarose gel.
    static double migration(double size) {
        const double u = 4.2 - std::log10(size);
        return 30 + 150 * u + 35 * u * u;
    }
};
