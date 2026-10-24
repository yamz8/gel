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

    // A believable stained agarose gel for screenshots and the window tests:
    // a 1 kb ladder, then a PCR series whose product grows lane by lane, a
    // fainter second product, and a primer-dimer smear at the bottom.
    static Synthetic demo() {
        Synthetic gel;
        gel.width = 760;
        gel.height = 470;
        gel.background = 2600;
        gel.slopeY = 1.5;
        gel.noise = 140;
        gel.seed = 11;
        SyntheticLane ladder{70, 41, {}};
        for (double size : {10000.0, 8000.0, 6000.0, 5000.0, 4000.0, 3000.0, 2000.0, 1500.0, 1000.0, 500.0})
            ladder.bands << SyntheticBand{migration(size), size == 3000 ? 16000.0 : 9000.0, 3.2};
        gel.lanes << ladder;
        const double amounts[] = {0.15, 0.3, 0.55, 0.8, 1.0, 1.0, 0.45};
        for (int i = 0; i < 7; ++i) {
            SyntheticLane lane{150.0 + 82 * i, 41, {}};
            lane.bands << SyntheticBand{migration(2500), 20000 * amounts[i], 4};
            if (i >= 2)
                lane.bands << SyntheticBand{migration(1200), 5000 * amounts[i], 3.5};
            lane.bands << SyntheticBand{migration(180), 3500, 9};
            gel.lanes << lane;
        }
        return gel;
    }

    // Where a ladder band of this size runs: log-linear with a gentle bend,
    // like a real agarose gel.
    static double migration(double size) {
        const double u = 4.2 - std::log10(size);
        return 30 + 150 * u + 35 * u * u;
    }
};
