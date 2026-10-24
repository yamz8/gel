#include "analysis.h"

#include <QCoreApplication>
#include <QLocale>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Analysis {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("Analysis", text); }

double median(std::vector<double> values) {
    if (values.empty())
        return 0;
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    return values[mid];
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty())
        return 0;
    const size_t k = size_t(std::clamp(fraction, 0.0, 1.0) * (values.size() - 1));
    std::nth_element(values.begin(), values.begin() + k, values.end());
    return values[k];
}

// How far each local maximum stands above the higher of the two lowest
// points separating it from a taller neighbor (topographic prominence).
double prominence(const std::vector<double> &s, int i) {
    double leftMin = s[i], rightMin = s[i];
    for (int j = i - 1; j >= 0 && s[j] <= s[i]; --j)
        leftMin = std::min(leftMin, s[j]);
    for (int j = i + 1; j < int(s.size()) && s[j] <= s[i]; ++j)
        rightMin = std::min(rightMin, s[j]);
    // A side that runs off the end without meeting a taller peak counts its
    // own minimum, as scipy and ImageJ do.
    return s[i] - std::max(leftMin, rightMin);
}

std::vector<int> localMaxima(const std::vector<double> &s) {
    std::vector<int> peaks;
    const int n = int(s.size());
    for (int i = 1; i < n - 1; ++i) {
        if (!(s[i] > s[i - 1]))
            continue;
        // A flat top counts once, at its middle.
        int j = i;
        while (j + 1 < n && s[j + 1] == s[i])
            ++j;
        if (j + 1 < n && s[j + 1] < s[i])
            peaks.push_back((i + j) / 2);
        i = j;
    }
    return peaks;
}

double noiseLevel(const std::vector<double> &values) {
    if (values.size() < 3)
        return 0;
    std::vector<double> diffs(values.size() - 1);
    for (size_t i = 0; i + 1 < values.size(); ++i)
        diffs[i] = values[i + 1] - values[i];
    const double m = median(diffs);
    for (double &d : diffs)
        d = std::abs(d - m);
    // MAD of first differences, scaled to a Gaussian sigma of the values.
    return 1.4826 * median(diffs) / std::sqrt(2.0);
}

// Walks from a peak until the signal fades out or clearly rises again
// toward a neighbor; the bound is the lowest point passed.
int walk(const std::vector<double> &s, int peak, int step, double tolerance) {
    const double floor = .03 * s[peak];
    int j = peak, lowest = peak;
    while (j + step >= 0 && j + step < int(s.size())) {
        j += step;
        if (s[j] < s[lowest])
            lowest = j;
        if (s[j] <= floor)
            return j;
        if (s[j] > s[lowest] + tolerance)
            break;
    }
    return lowest;
}

Band bandFromPeak(const Profile &profile, const std::vector<double> &s, int peak, double tolerance) {
    const int lower = walk(s, peak, -1, tolerance), upper = walk(s, peak, 1, tolerance);
    // Parabolic interpolation puts the peak between samples.
    double offset = 0;
    if (peak > 0 && peak + 1 < int(s.size())) {
        const double a = s[peak - 1], b = s[peak], c = s[peak + 1];
        const double denominator = a - 2 * b + c;
        if (denominator < 0)
            offset = std::clamp(.5 * (a - c) / denominator, -.5, .5);
    }
    return Band{profile.top + peak + offset, profile.top + lower, profile.top + upper, false};
}

std::vector<double> polyfit(const std::vector<double> &t, const std::vector<double> &y, int degree) {
    const int m = degree + 1;
    std::vector<std::vector<double>> a(m, std::vector<double>(m + 1, 0));
    for (size_t k = 0; k < t.size(); ++k) {
        std::vector<double> powers(2 * m, 1);
        for (int p = 1; p < 2 * m; ++p)
            powers[p] = powers[p - 1] * t[k];
        for (int r = 0; r < m; ++r) {
            for (int c = 0; c < m; ++c)
                a[r][c] += powers[r + c];
            a[r][m] += powers[r] * y[k];
        }
    }
    // Gauss-Jordan with partial pivoting; the system is at most 4 by 4.
    for (int col = 0; col < m; ++col) {
        int pivot = col;
        for (int r = col + 1; r < m; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col]))
                pivot = r;
        std::swap(a[col], a[pivot]);
        if (std::abs(a[col][col]) < 1e-12)
            return {};
        for (int r = 0; r < m; ++r) {
            if (r == col)
                continue;
            const double f = a[r][col] / a[col][col];
            for (int c = col; c <= m; ++c)
                a[r][c] -= f * a[col][c];
        }
    }
    std::vector<double> coefficients(m);
    for (int r = 0; r < m; ++r)
        coefficients[r] = a[r][m] / a[r][r];
    return coefficients;
}

double evaluate(const std::vector<double> &c, double t) {
    double v = 0;
    for (int i = int(c.size()) - 1; i >= 0; --i)
        v = v * t + c[i];
    return v;
}
} // namespace

bool darkBands(const GelImage &image) {
    // Bands are the long tail of the histogram: above the background for
    // bright bands, below it for dark ones. Taking out each row's and then
    // each column's median first keeps a gradient across the scan from
    // looking like a tail.
    const int w = image.width(), h = image.height();
    const int step = std::max(1, std::max(w, h) / 400);
    const int cols = (w + step - 1) / step, rows = (h + step - 1) / step;
    std::vector<double> grid(size_t(cols) * rows), line;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            grid[size_t(r) * cols + c] = image.at(c * step, r * step);
    for (int r = 0; r < rows; ++r) {
        line.assign(grid.begin() + size_t(r) * cols, grid.begin() + size_t(r + 1) * cols);
        const double m = median(line);
        for (int c = 0; c < cols; ++c)
            grid[size_t(r) * cols + c] -= m;
    }
    for (int c = 0; c < cols; ++c) {
        line.clear();
        for (int r = 0; r < rows; ++r)
            line.push_back(grid[size_t(r) * cols + c]);
        const double m = median(line);
        for (int r = 0; r < rows; ++r)
            grid[size_t(r) * cols + c] -= m;
    }
    const double low = percentile(grid, .002), high = percentile(grid, .998), mid = median(grid);
    return mid - low > high - mid;
}

bool resolveDark(const GelImage &image, Polarity polarity) {
    return polarity == Polarity::Dark || (polarity == Polarity::Auto && darkBands(image));
}

std::vector<double> smooth(const std::vector<double> &values, double sigma) {
    if (sigma <= 0 || values.size() < 2)
        return values;
    const int radius = int(std::ceil(3 * sigma));
    std::vector<double> kernel(2 * radius + 1);
    for (int i = -radius; i <= radius; ++i)
        kernel[i + radius] = std::exp(-.5 * i * i / (sigma * sigma));
    const double total = std::accumulate(kernel.begin(), kernel.end(), 0.0);
    const int n = int(values.size());
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) {
        double sum = 0;
        for (int k = -radius; k <= radius; ++k)
            sum += kernel[k + radius] * values[std::clamp(i + k, 0, n - 1)];
        out[i] = sum / total;
    }
    return out;
}

std::vector<double> rollingBall(const std::vector<double> &input, double radius) {
    const int n = int(input.size());
    if (n < 2)
        return input;
    // A ball rolled straight along a sloped background rides up into each
    // band by about the slope times the band's width. So take the slope out
    // first: fit a line under the profile, clipping it to the line between
    // fits so bands stop pulling it up, roll the ball on what is left, and
    // add the line back.
    std::vector<double> clipped = input;
    double a = 0, b = 0;
    const double meanI = (n - 1) / 2.0;
    double sii = 0;
    for (int i = 0; i < n; ++i)
        sii += (i - meanI) * (i - meanI);
    for (int iteration = 0; iteration < 30; ++iteration) {
        double mean = 0, siy = 0;
        for (int i = 0; i < n; ++i)
            mean += clipped[i];
        mean /= n;
        for (int i = 0; i < n; ++i)
            siy += (i - meanI) * (clipped[i] - mean);
        b = siy / sii;
        a = mean - b * meanI;
        for (int i = 0; i < n; ++i)
            clipped[i] = std::min(clipped[i], a + b * i);
    }
    std::vector<double> profile(n);
    for (int i = 0; i < n; ++i)
        profile[i] = input[i] - (a + b * i);

    // The ball rolls on the profile drawn ten times taller than it is long,
    // so the same radius gives the same background whether the scan is 8 or
    // 16 bits, and the ball is flat enough to stay out of the bands.
    const auto [lo, hi] = std::minmax_element(profile.begin(), profile.end());
    const double range = *hi - *lo;
    std::vector<double> out(n);
    if (range <= 0) {
        for (int i = 0; i < n; ++i)
            out[i] = input[i];
        return out;
    }
    const double k = 10.0 * n / range;
    std::vector<double> p(n);
    for (int i = 0; i < n; ++i)
        p[i] = (profile[i] - *lo) * k;

    const double r = std::max(1.0, radius);
    const int ri = int(std::ceil(r));
    std::vector<double> cap(2 * ri + 1);
    for (int d = -ri; d <= ri; ++d)
        cap[d + ri] = std::abs(d) <= r ? std::sqrt(r * r - double(d) * d) : -1e300;

    // The highest each ball can sit with its top still under the profile,
    // then the upper envelope of all those balls. Balls may roll past either
    // end, so the background near the ends isn't pulled down.
    std::vector<double> centers(n + 2 * ri);
    for (int c = -ri; c < n + ri; ++c) {
        double z = 1e300;
        for (int j = std::max(0, c - ri); j <= std::min(n - 1, c + ri); ++j)
            if (cap[j - c + ri] > -1e299)
                z = std::min(z, p[j] - cap[j - c + ri]);
        centers[c + ri] = z;
    }
    for (int i = 0; i < n; ++i) {
        double ball = -1e300;
        for (int c = i - ri; c <= i + ri; ++c)
            if (cap[i - c + ri] > -1e299 && centers[c + ri] < 1e299)
                ball = std::max(ball, centers[c + ri] + cap[i - c + ri]);
        out[i] = std::min(ball, p[i]) / k + *lo + a + b * i;
    }
    return out;
}

Profile laneProfile(const Frame &frame, const Lane &lane, bool dark, const Quantification &quant) {
    Profile profile;
    const GelImage *image = frame.image();
    const double maxValue = image->maxValue();
    const double top = std::max(0.0, std::round(lane.top));
    const double bottom = std::min(double(frame.height() - 1), std::round(lane.bottom));
    profile.top = top;
    const int rows = std::max(0, int(bottom - top) + 1);
    const int columns = std::max(1, int(std::round(lane.width)));
    const double left = lane.center - lane.width / 2;
    const double step = lane.width / columns;

    profile.raw.resize(rows);
    profile.saturated.resize(rows);
    for (int i = 0; i < rows; ++i) {
        double sum = 0;
        int saturated = 0;
        for (int j = 0; j < columns; ++j) {
            const double raw = frame.sample(left + (j + .5) * step, top + i);
            if (dark ? raw <= .5 : raw >= maxValue - .5)
                ++saturated;
            sum += signal(raw, maxValue, dark);
        }
        profile.raw[i] = sum / columns;
        profile.saturated[i] = saturated;
    }

    if (quant.background == BackgroundMethod::RollingBall) {
        profile.background = rollingBall(profile.raw, quant.ballRadius);
    } else if (quant.background == BackgroundMethod::LaneEdges) {
        // Thin strips just outside both edges, averaged per row and smoothed,
        // follow a background that changes down the gel.
        const double strip = std::max(3.0, lane.width * .15), gap = 2;
        std::vector<double> background(rows);
        for (int i = 0; i < rows; ++i) {
            double sum = 0;
            int count = 0;
            for (const double start : {left - gap - strip, left + lane.width + gap}) {
                if (start < 0 || start + strip > frame.width() - 1)
                    continue;
                for (double x = start + .5; x < start + strip; x += 1) {
                    sum += signal(frame.sample(x, top + i), maxValue, dark);
                    ++count;
                }
            }
            background[i] = count ? sum / count : 0;
        }
        profile.background = smooth(background, 3);
    } else {
        profile.background.assign(rows, 0);
    }

    profile.net.resize(rows);
    for (int i = 0; i < rows; ++i)
        profile.net[i] = profile.raw[i] - profile.background[i];
    return profile;
}

QList<Lane> detectLanes(const Frame &frame, bool dark, QRectF region) {
    region = region.isEmpty() ? frame.rect() : region.intersected(frame.rect());
    const int x0 = int(std::ceil(region.left())), x1 = int(std::floor(region.right())) - 1;
    const int y0 = int(std::ceil(region.top())), y1 = int(std::floor(region.bottom())) - 1;
    if (x1 - x0 < 8 || y1 - y0 < 8)
        return {};
    const int width = x1 - x0 + 1;
    const double maxValue = frame.image()->maxValue();

    // Column profile of what stands above each row's own background, so a
    // gradient down the gel doesn't hide the gaps between lanes.
    std::vector<double> columns(width, 0), row(width);
    const int rowStep = std::max(1, (y1 - y0) / 600);
    int rowsUsed = 0;
    for (int y = y0; y <= y1; y += rowStep) {
        for (int x = 0; x < width; ++x)
            row[x] = signal(frame.sample(x0 + x, y), maxValue, dark);
        const double background = percentile(row, .2);
        for (int x = 0; x < width; ++x)
            columns[x] += std::max(0.0, row[x] - background);
        ++rowsUsed;
    }
    for (double &c : columns)
        c /= rowsUsed;

    const std::vector<double> s = smooth(columns, std::max(1.0, width / 300.0));
    const auto [lo, hi] = std::minmax_element(s.begin(), s.end());
    const double base = *lo, range = *hi - *lo;
    if (range <= 1e-9)
        return {};

    struct Found { double left, right; };
    std::vector<Found> found;
    // Faint lanes count too, as long as they stand clear of the noise.
    const double minimum = std::max(.08 * range, 4 * noiseLevel(columns));
    for (const int peak : localMaxima(s)) {
        if (prominence(s, peak) < minimum)
            continue;
        const double half = base + .5 * (s[peak] - base);
        int l = peak, r = peak;
        while (l > 0 && s[l - 1] >= half)
            --l;
        while (r < width - 1 && s[r + 1] >= half)
            ++r;
        // Sub-pixel edges where the profile crosses half height, in the
        // pixel-center coordinates the lane is sampled in.
        const double left = l > 0 ? l - (s[l] - half) / std::max(1e-12, s[l] - s[l - 1]) : l - .5;
        const double right = r < width - 1 ? r + (s[r] - half) / std::max(1e-12, s[r] - s[r + 1]) : r + .5;
        if (!found.empty() && left < found.back().right)
            continue;
        if (right - left >= 3)
            found.push_back({left, right});
    }
    if (found.empty())
        return {};

    // Lanes are cast by one comb, so give them all the typical width, kept
    // clear of their neighbors.
    std::vector<double> widths;
    for (const auto &f : found)
        widths.push_back(f.right - f.left);
    double laneWidth = median(widths);
    for (size_t i = 1; i < found.size(); ++i) {
        const double pitch = (found[i].left + found[i].right) / 2 - (found[i - 1].left + found[i - 1].right) / 2;
        laneWidth = std::min(laneWidth, .95 * pitch);
    }

    QList<Lane> lanes;
    for (const auto &f : found) {
        Lane lane;
        lane.center = x0 + (f.left + f.right) / 2;
        lane.width = laneWidth;
        lane.top = y0;
        lane.bottom = y1;
        lane.label = QString::number(lanes.size() + 1);
        lanes << lane;
    }
    return lanes;
}

QList<Band> detectBands(const Profile &profile) {
    const std::vector<double> s = smooth(profile.net, 1.5);
    if (s.size() < 5)
        return {};
    const double top = *std::max_element(s.begin(), s.end());
    if (top <= 0)
        return {};
    const double noise = noiseLevel(profile.net);
    const double threshold = std::max(3 * noise, .03 * top);

    QList<Band> bands;
    for (const int peak : localMaxima(s)) {
        if (s[peak] < threshold || prominence(s, peak) < threshold)
            continue;
        bands << bandFromPeak(profile, s, peak, std::max(noise, .02 * s[peak]));
    }
    // A valley belongs to the band above it, so no row is counted twice.
    for (int i = 1; i < bands.size(); ++i)
        if (bands[i].lower <= bands[i - 1].upper)
            bands[i].lower = bands[i - 1].upper + 1;
    return bands;
}

Band bandAt(const Profile &profile, double y) {
    const std::vector<double> s = smooth(profile.net, 1.5);
    const int n = int(s.size());
    const int index = std::clamp(int(std::lround(y - profile.top)), 0, std::max(0, n - 1));
    if (n < 3)
        return Band{y, y - 3, y + 3, true};
    int peak = index;
    for (int i = std::max(0, index - 4); i <= std::min(n - 1, index + 4); ++i)
        if (s[i] > s[peak])
            peak = i;
    Band band;
    if (s[peak] <= 0)
        band = Band{profile.top + index, profile.top + std::max(0, index - 3),
                    profile.top + std::min(n - 1, index + 3), true};
    else
        band = bandFromPeak(profile, s, peak, std::max(noiseLevel(profile.net), .02 * s[peak]));
    band.manual = true;
    return band;
}

LadderFit fitLadder(const QList<double> &positionsIn, const QList<double> &sizesIn) {
    LadderFit fit;
    QList<double> positions = positionsIn, sizes = sizesIn;
    std::sort(positions.begin(), positions.end());
    std::sort(sizes.begin(), sizes.end(), std::greater<>());
    const int n = std::min(positions.size(), sizes.size());
    if (n < 2) {
        fit.message = tr("The ladder needs at least two bands with sizes.");
        return fit;
    }
    if (positions.size() != sizes.size())
        fit.message = tr("The ladder lane has %1 bands but the ladder lists %2 sizes; matched from the top.")
                          .arg(positions.size())
                          .arg(sizes.size());
    fit.positions = positions.mid(0, n);
    fit.sizes = sizes.mid(0, n);

    std::vector<double> y, logSize;
    for (int i = 0; i < n; ++i) {
        y.push_back(fit.positions[i]);
        logSize.push_back(std::log10(fit.sizes[i]));
    }
    const double mean = std::accumulate(y.begin(), y.end(), 0.0) / n;
    double spread = 0;
    for (double v : y)
        spread += (v - mean) * (v - mean);
    spread = std::sqrt(spread / n);
    if (spread <= 0) {
        fit.message = tr("The ladder bands are all at one position.");
        return fit;
    }
    fit.offset = mean;
    fit.scale = spread;
    fit.minPosition = y.front();
    fit.maxPosition = y.back();
    std::vector<double> t;
    for (double v : y)
        t.push_back((v - mean) / spread);

    // Size against migration is close to log-linear and bends at the ends, so
    // use a cubic when there are enough points, and drop a degree whenever
    // the curve would stop shrinking toward the bottom of the gel.
    for (int degree = n >= 6 ? 3 : n >= 4 ? 2 : 1; degree >= 1; --degree) {
        const auto c = polyfit(t, logSize, degree);
        if (c.empty())
            continue;
        bool monotonic = true;
        for (int i = 0; i <= 50 && monotonic; ++i) {
            const double a = t.front() + (t.back() - t.front()) * i / 51.0;
            const double b = t.front() + (t.back() - t.front()) * (i + 1) / 51.0;
            monotonic = evaluate(c, b) < evaluate(c, a);
        }
        if (!monotonic && degree > 1)
            continue;
        fit.coefficients = c;
        fit.degree = degree;
        break;
    }
    if (fit.coefficients.empty()) {
        fit.message = tr("The ladder could not be fitted.");
        return fit;
    }
    const double meanLog = std::accumulate(logSize.begin(), logSize.end(), 0.0) / n;
    double residual = 0, total = 0;
    for (int i = 0; i < n; ++i) {
        residual += std::pow(logSize[i] - evaluate(fit.coefficients, t[i]), 2);
        total += std::pow(logSize[i] - meanLog, 2);
    }
    fit.r2 = total > 0 ? 1 - residual / total : 1;
    fit.ok = true;
    return fit;
}

double estimateSize(const LadderFit &fit, double position) {
    if (!fit.ok)
        return qQNaN();
    return std::pow(10.0, evaluate(fit.coefficients, (position - fit.offset) / fit.scale));
}

QList<BandResult> Results::forLane(int lane) const {
    QList<BandResult> out;
    for (const auto &b : bands)
        if (b.lane == lane)
            out << b;
    return out;
}

Results quantify(const Frame &frame, const Session &session) {
    Results results;
    results.dark = resolveDark(*frame.image(), session.quant.polarity);

    QList<QList<Band>> sorted;
    for (const auto &lane : session.lanes) {
        results.profiles << laneProfile(frame, lane, results.dark, session.quant);
        QList<Band> bands = lane.bands;
        std::sort(bands.begin(), bands.end(), [](const Band &a, const Band &b) { return a.position < b.position; });
        sorted << bands;
    }

    const int ladderLane = session.ladder.lane;
    const bool hasLadder = ladderLane >= 0 && ladderLane < session.lanes.size() && !session.ladder.sizes.isEmpty();
    if (hasLadder) {
        QList<double> positions;
        for (const auto &band : sorted[ladderLane])
            positions << band.position;
        results.fit = fitLadder(positions, session.ladder.sizes);
        if (!results.fit.message.isEmpty())
            results.warnings << results.fit.message;
    }

    for (int l = 0; l < session.lanes.size(); ++l) {
        const Profile &profile = results.profiles[l];
        const int rows = int(profile.net.size());
        for (int b = 0; b < sorted[l].size(); ++b) {
            const Band &band = sorted[l][b];
            BandResult r;
            r.lane = l;
            r.band = b;
            r.laneLabel = session.lanes[l].label;
            r.position = band.position;
            const int from = std::clamp(int(std::lround(band.lower - profile.top)), 0, rows);
            const int to = std::clamp(int(std::lround(band.upper - profile.top)), -1, rows - 1);
            double area = 0;
            for (int i = from; i <= to; ++i) {
                area += profile.net[i];
                r.saturatedPixels += profile.saturated[i];
            }
            // The profile is a per-row mean across the lane, so times the
            // width it is the band's summed, background-free pixel values.
            r.volume = area * session.lanes[l].width;
            r.ladder = hasLadder && l == ladderLane;
            if (r.ladder && b < results.fit.sizes.size())
                r.ladderSize = results.fit.sizes[b];
            if (results.fit.ok) {
                r.size = estimateSize(results.fit, band.position);
                const double margin = .02 * (results.fit.maxPosition - results.fit.minPosition);
                r.extrapolated = band.position < results.fit.minPosition - margin ||
                                 band.position > results.fit.maxPosition + margin;
            }
            if (r.saturatedPixels > 0)
                results.warnings << tr("Lane %1, band %2 has %3 saturated pixels; its volume is underestimated.")
                                        .arg(r.laneLabel)
                                        .arg(b + 1)
                                        .arg(r.saturatedPixels);
            results.bands << r;
        }
    }

    const auto &q = session.quant;
    double reference = qQNaN();
    if (q.normalization == Normalization::Lane) {
        if (q.referenceLane >= 0 && q.referenceLane < session.lanes.size()) {
            reference = 0;
            for (const auto &r : results.bands)
                if (r.lane == q.referenceLane)
                    reference += r.volume;
        }
        if (!(reference > 0))
            results.warnings << tr("Pick a reference lane with bands to normalize to.");
    } else if (q.normalization == Normalization::Band) {
        for (const auto &r : results.bands)
            if (r.lane == q.referenceLane && r.band == q.referenceBand)
                reference = r.volume;
        if (!(reference > 0))
            results.warnings << tr("Pick a reference band with signal to normalize to.");
    }
    if (reference > 0)
        for (auto &r : results.bands)
            r.normalized = r.volume / reference;
    return results;
}

QString formatSize(double size, const QString &unit) {
    if (!std::isfinite(size))
        return {};
    const QLocale locale;
    auto significant = [&](double v) {
        // Three significant figures, without trailing zeros.
        const int decimals = v >= 100 ? 0 : v >= 10 ? 1 : 2;
        QString text = locale.toString(v, 'f', decimals);
        if (decimals > 0) {
            while (text.endsWith(locale.zeroDigit()))
                text.chop(1);
            if (text.endsWith(locale.decimalPoint()))
                text.chop(1);
        }
        return text;
    };
    if (unit == "bp" && size >= 1000)
        return significant(size / 1000) + " kb";
    return significant(size) + " " + unit;
}

} // namespace Analysis
