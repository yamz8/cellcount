#include "detect.h"

#include <QFileInfo>
#include <QImageReader>
#include <QThread>
#include <QtConcurrentMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

QStringList imageSuffixes() { return {"png", "jpg", "jpeg", "tif", "tiff"}; }

namespace {
// Maps 16-bit samples onto 8 bits between two percentiles of the histogram.
QImage stretch16(const QImage &image) {
    std::vector<quint32> histogram(65536, 0);
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const quint16 *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            ++histogram[line[x]];
    }
    const quint64 pixels = quint64(image.width()) * image.height();
    auto percentile = [&](double fraction) {
        const quint64 target = quint64(fraction * pixels);
        quint64 seen = 0;
        for (int value = 0; value < 65536; ++value) {
            seen += histogram[value];
            if (seen > target)
                return value;
        }
        return 65535;
    };
    const int low = percentile(0.001);
    const int high = std::max(low + 1, percentile(0.999));
    QImage out(image.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y) {
        const auto *in = reinterpret_cast<const quint16 *>(image.constScanLine(y));
        uchar *line = out.scanLine(y);
        for (int x = 0; x < image.width(); ++x)
            line[x] = uchar(std::clamp((in[x] - low) * 255 / (high - low), 0, 255));
    }
    return out;
}
} // namespace

QImage loadImage(const QString &path, QString *error) {
    if (!imageSuffixes().contains(QFileInfo(path).suffix().toLower())) {
        if (error)
            *error = QObject::tr("Only PNG, JPEG and TIFF images can be counted.");
        return {};
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // Microscope and plate scans can be large; let Qt read them whole.
    reader.setAllocationLimit(2048);
    QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = QObject::tr("Could not read %1: %2").arg(QFileInfo(path).fileName(), reader.errorString());
        return {};
    }
    if (image.format() == QImage::Format_Grayscale16)
        return stretch16(image);
    if (image.depth() > 32 || image.format() == QImage::Format_RGBX64 || image.format() == QImage::Format_RGBA64)
        return image.convertToFormat(QImage::Format_ARGB32);
    return image;
}

namespace {
// Runs work(first, last) over bands of rows on every core.
template <typename Work>
void inBands(int rows, Work work) {
    const int band = std::max(16, rows / (4 * std::max(1, QThread::idealThreadCount())));
    std::vector<int> starts;
    for (int r = 0; r < rows; r += band)
        starts.push_back(r);
    QtConcurrent::blockingMap(starts, [&](int start) { work(start, std::min(rows, start + band)); });
}

// A grayscale image as floats, so blurring and differences keep their precision.
struct Plane {
    int width = 0, height = 0;
    std::vector<float> values;
    Plane() = default;
    Plane(int w, int h) : width(w), height(h), values(size_t(w) * h, 0.f) {}
    float &at(int x, int y) { return values[size_t(y) * width + x]; }
    float at(int x, int y) const { return values[size_t(y) * width + x]; }
};

Plane toPlane(const QImage &image) {
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    Plane plane(gray.width(), gray.height());
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            plane.at(x, y) = line[x];
    }
    return plane;
}

Plane halve(const Plane &in) {
    Plane out(std::max(1, in.width / 2), std::max(1, in.height / 2));
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const int x1 = std::min(2 * x + 1, in.width - 1), y1 = std::min(2 * y + 1, in.height - 1);
            out.at(x, y) = (in.at(2 * x, 2 * y) + in.at(x1, 2 * y) + in.at(2 * x, y1) + in.at(x1, y1)) / 4;
        }
    return out;
}

// A separable Gaussian blur, with the edges extended.
Plane blur(const Plane &in, double sigma) {
    if (sigma < 0.3)
        return in;
    const int radius = int(std::ceil(3 * sigma));
    std::vector<float> kernel(2 * radius + 1);
    float sum = 0;
    for (int i = -radius; i <= radius; ++i)
        sum += kernel[i + radius] = float(std::exp(-i * i / (2 * sigma * sigma)));
    for (auto &k : kernel)
        k /= sum;
    Plane across(in.width, in.height), out(in.width, in.height);
    inBands(in.height, [&](int first, int last) {
        for (int y = first; y < last; ++y)
            for (int x = 0; x < in.width; ++x) {
                float v = 0;
                for (int i = -radius; i <= radius; ++i)
                    v += kernel[i + radius] * in.at(std::clamp(x + i, 0, in.width - 1), y);
                across.at(x, y) = v;
            }
    });
    inBands(in.height, [&](int first, int last) {
        for (int y = first; y < last; ++y)
            for (int x = 0; x < in.width; ++x) {
                float v = 0;
                for (int i = -radius; i <= radius; ++i)
                    v += kernel[i + radius] * across.at(x, std::clamp(y + i, 0, in.height - 1));
                out.at(x, y) = v;
            }
    });
    return out;
}

// The spread of a level's response away from any object: its median absolute
// deviation, scaled to a standard deviation, from a sample of the pixels.
float noiseOf(const Plane &plane) {
    std::vector<float> sample;
    const size_t step = std::max<size_t>(1, plane.values.size() / 200000);
    for (size_t i = 0; i < plane.values.size(); i += step)
        sample.push_back(plane.values[i]);
    auto middle = sample.begin() + sample.size() / 2;
    std::nth_element(sample.begin(), middle, sample.end());
    const float median = *middle;
    for (auto &v : sample)
        v = std::abs(v - median);
    std::nth_element(sample.begin(), middle, sample.end());
    return 1.4826f * *middle;
}

// Running minimum or maximum over a window of length (2 * half + 1) along
// rows (step 1) or columns (step width), in constant time per pixel (van
// Herk / Gil-Werman), with the edges extended.
template <typename Pick>
void runningExtreme(const Plane &in, Plane &out, int half, bool rows, Pick pick) {
    const int length = rows ? in.width : in.height, lines = rows ? in.height : in.width;
    const int window = 2 * half + 1;
    inBands(lines, [&](int first, int last) {
    std::vector<float> line(length + 2 * half), forward(line.size()), backward(line.size());
    for (int l = first; l < last; ++l) {
        auto get = [&](int i) { return rows ? in.at(std::clamp(i, 0, length - 1), l) : in.at(l, std::clamp(i, 0, length - 1)); };
        for (int i = 0; i < int(line.size()); ++i)
            line[i] = get(i - half);
        for (int i = 0; i < int(line.size()); ++i)
            forward[i] = i % window == 0 ? line[i] : pick(forward[i - 1], line[i]);
        for (int i = int(line.size()) - 1; i >= 0; --i)
            backward[i] = (i + 1) % window == 0 || i + 1 == int(line.size()) ? line[i] : pick(backward[i + 1], line[i]);
        for (int i = 0; i < length; ++i) {
            const float v = pick(backward[i], forward[std::min(i + window - 1, int(line.size()) - 1)]);
            rows ? out.at(i, l) = v : out.at(l, i) = v;
        }
    }
    });
}

// Swaps rows and columns in cache-sized tiles.
Plane transposed(const Plane &in) {
    Plane out(in.height, in.width);
    constexpr int tile = 32;
    inBands((in.height + tile - 1) / tile, [&](int first, int last) {
        for (int ty = first * tile; ty < std::min(in.height, last * tile); ty += tile)
            for (int tx = 0; tx < in.width; tx += tile)
                for (int y = ty; y < std::min(in.height, ty + tile); ++y)
                    for (int x = tx; x < std::min(in.width, tx + tile); ++x)
                        out.at(y, x) = in.at(x, y);
    });
    return out;
}

// Columns go through the row pass on a transposed copy: walking down a
// column of a large image misses the cache on every pixel.
Plane extreme(const Plane &in, int half, bool rows, bool maximum) {
    const Plane source = rows ? Plane() : transposed(in);
    const Plane &along = rows ? in : source;
    Plane out(along.width, along.height);
    if (maximum)
        runningExtreme(along, out, half, true, [](float a, float b) { return std::max(a, b); });
    else
        runningExtreme(along, out, half, true, [](float a, float b) { return std::min(a, b); });
    return rows ? out : transposed(out);
}

// Erases long, thin, straight dark lines, such as a hemocytometer's grid, by
// filling them with their surroundings. A line is a run of something thinner
// than a few pixels (a top-hat) that stays straight for longer than any cell
// is wide; cell rims curve, and colonies are too thick, so both are kept.
// Lines up to a few degrees off the image axes are caught.
Plane withoutLines(const Plane &gray, int length) {
    const int thin = 3; // a 7-pixel square: structures narrower than that
    auto open = [](const Plane &p, int half, bool rows) { return extreme(extreme(p, half, rows, false), half, rows, true); };
    auto close = [](const Plane &p, int half, bool rows) { return extreme(extreme(p, half, rows, true), half, rows, false); };

    // Blurring widens a slightly tilted line, so it still runs unbroken
    // through a straight window. Noise alone gives every pixel a little thin
    // structure, so a line must stand well clear of the image's own level of it.
    auto lineMask = [&](const Plane &thinParts) {
        const Plane soft = blur(thinParts, 1.0);
        const Plane across = open(soft, length / 2, true), down = open(soft, length / 2, false);
        Plane strength(gray.width, gray.height);
        for (size_t i = 0; i < strength.values.size(); ++i)
            strength.values[i] = std::max(across.values[i], down.values[i]);
        std::vector<float> sample;
        for (size_t i = 0; i < strength.values.size(); i += std::max<size_t>(1, strength.values.size() / 100000))
            sample.push_back(strength.values[i]);
        std::nth_element(sample.begin(), sample.begin() + sample.size() / 2, sample.end());
        const float floor = sample[sample.size() / 2];
        const float limit = floor + std::max(6.f, 2 * floor);
        Plane mask(gray.width, gray.height);
        for (size_t i = 0; i < mask.values.size(); ++i)
            mask.values[i] = strength.values[i] > limit ? 1.f : 0.f;
        return extreme(extreme(mask, 1, true, true), 1, false, true);
    };

    // Only dark lines are erased: a hemocytometer's grid is dark in
    // brightfield, while a bright line is usually the rim of a dish, which
    // curves and is better left whole. Where a line runs, the pixel and its
    // neighbours take the value with thin dark things removed, which also
    // clears the crossings, the antialiased edges, and the narrow bright gaps
    // between triple lines.
    Plane out = gray;
    const Plane closed = close(close(out, thin, true), thin, false);
    Plane dark(gray.width, gray.height);
    for (size_t i = 0; i < out.values.size(); ++i)
        dark.values[i] = closed.values[i] - out.values[i];
    const Plane mask = lineMask(dark);
    for (size_t i = 0; i < out.values.size(); ++i)
        if (mask.values[i] > 0)
            out.values[i] = closed.values[i];
    return out;
}

struct Level {
    Plane response; // positive where the image has a bright blob of this size
    int shift = 0;  // the level's pixels are 2^shift image pixels
    double sigma = 0;
    float noise = 0;
};

struct Candidate {
    double x, y, radius, contrast;
    double area = 0;
};

// A disc of contrast C answers with about 2C/e at its best scale (the
// scale-normalised Laplacian of a blurred step), so dividing by this reads the
// response back as contrast in gray levels.
constexpr double discResponse = 0.7358;
// How elongated a peak may be before it's taken for a line or an edge (the
// ratio of its principal curvatures, as in SIFT).
constexpr double maxElongation = 6;
// An automatic threshold sits this many noise deviations above the background.
constexpr double noiseDeviations = 5;

struct Measured {
    double contrast = 0;
    double area = 0; // in image pixels
};

// The spot as it is in the image itself: how much its middle stands out from
// the ring around it, and how many pixels around it stand out by at least
// half that. The spot filter also answers beside strong objects of the other
// kind (the background hugging a dark rim looks bright next to it), and the
// contrast tells those echoes apart from real spots; the ring's median
// ignores a neighbouring cell that covers part of it. The area is what the
// smallest and largest cell settings are held to, so a speck of dust is left
// out however strongly it shows.
Measured measure(const Plane &gray, Candidate c, int sign, int shift) {
    // Measured on the image halved as often as the spot's own level, which
    // keeps a large spot as cheap to check as a small one.
    const double s = double(1 << shift);
    c = Candidate{c.x / s, c.y / s, c.radius / s, c.contrast};
    double inside = 0;
    int insideCount = 0;
    std::vector<float> ring;
    const double inner = std::max(1.0, 0.7 * c.radius), from = 1.3 * c.radius + 1, to = 2.2 * c.radius + 2;
    for (int y = int(c.y - to); y <= int(c.y + to); ++y)
        for (int x = int(c.x - to); x <= int(c.x + to); ++x) {
            if (x < 0 || y < 0 || x >= gray.width || y >= gray.height)
                continue;
            const double d = std::hypot(x + 0.5 - c.x, y + 0.5 - c.y);
            if (d <= inner) {
                inside += gray.at(x, y);
                ++insideCount;
            } else if (d >= from && d <= to) {
                ring.push_back(gray.at(x, y));
            }
        }
    Measured measured;
    if (!insideCount || ring.empty())
        return measured;
    auto middle = ring.begin() + ring.size() / 2;
    std::nth_element(ring.begin(), middle, ring.end());
    const float background = *middle;
    measured.contrast = sign * (inside / insideCount - background);
    if (measured.contrast <= 0)
        return measured;
    int count = 0;
    const double reach = 1.3 * c.radius + 1;
    for (int y = int(c.y - reach); y <= int(c.y + reach); ++y)
        for (int x = int(c.x - reach); x <= int(c.x + reach); ++x)
            if (x >= 0 && y >= 0 && x < gray.width && y < gray.height &&
                std::hypot(x + 0.5 - c.x, y + 0.5 - c.y) <= reach &&
                sign * (gray.at(x, y) - background) > measured.contrast / 2)
                ++count;
    measured.area = count * s * s;
    return measured;
}

QList<Candidate> findPeaks(const QList<Level> &levels, const QList<Plane> &pyramid, int sign, double threshold,
                           double minArea, double maxArea) {
    QList<Candidate> found;
    for (int i = 0; i + 1 < levels.size(); ++i) {
        const Level &level = levels[i];
        const Plane &d = level.response;
        const double scale = double(1 << level.shift);
        const float limit = float(threshold >= 0 ? threshold * discResponse
                                                 : std::max(2.0, noiseDeviations * level.noise));
        auto value = [&](const Level &l, double x, double y) {
            const double s = double(1 << l.shift);
            const int lx = std::clamp(int(x / s), 0, l.response.width - 1);
            const int ly = std::clamp(int(y / s), 0, l.response.height - 1);
            return sign * l.response.at(lx, ly);
        };
        for (int y = 1; y < d.height - 1; ++y)
            for (int x = 1; x < d.width - 1; ++x) {
                const float v = sign * d.at(x, y);
                if (v < limit)
                    continue;
                bool peak = true;
                for (int dy = -1; dy <= 1 && peak; ++dy)
                    for (int dx = -1; dx <= 1 && peak; ++dx)
                        if ((dx || dy) && sign * d.at(x + dx, y + dy) > v)
                            peak = false;
                if (!peak)
                    continue;
                const double ix = (x + 0.5) * scale, iy = (y + 0.5) * scale;
                // A peak in space must also be a peak across sizes, or it
                // belongs to a neighbouring scale.
                if ((i > 0 && value(levels[i - 1], ix, iy) > v) ||
                    (i + 1 < levels.size() && value(levels[i + 1], ix, iy) > v))
                    continue;
                const double dxx = sign * (d.at(x + 1, y) - 2 * d.at(x, y) + d.at(x - 1, y));
                const double dyy = sign * (d.at(x, y + 1) - 2 * d.at(x, y) + d.at(x, y - 1));
                const double dxy = sign * (d.at(x + 1, y + 1) - d.at(x + 1, y - 1) - d.at(x - 1, y + 1) +
                                           d.at(x - 1, y - 1)) / 4;
                const double trace = dxx + dyy, det = dxx * dyy - dxy * dxy;
                if (det <= 0 || trace * trace / det >= std::pow(maxElongation + 1, 2) / maxElongation)
                    continue;
                // Refine to a fraction of a pixel from the curvature.
                const double ox = dxx < 0 ? std::clamp(-(sign * (d.at(x + 1, y) - d.at(x - 1, y)) / 2) / dxx, -0.5, 0.5) : 0;
                const double oy = dyy < 0 ? std::clamp(-(sign * (d.at(x, y + 1) - d.at(x, y - 1)) / 2) / dyy, -0.5, 0.5) : 0;
                const Candidate candidate{(x + 0.5 + ox) * scale, (y + 0.5 + oy) * scale, level.sigma * M_SQRT2,
                                          v / discResponse};
                const Measured measured = measure(pyramid[level.shift], candidate, sign, level.shift);
                if (measured.contrast < std::max(2.0, 0.6 * limit / discResponse) || measured.area < minArea ||
                    (maxArea > 0 && measured.area > maxArea))
                    continue;
                found << Candidate{candidate.x, candidate.y, candidate.radius, candidate.contrast, measured.area};
            }
    }
    // The strongest answer wins where answers overlap: one cell seen at two
    // neighbouring sizes, or a ring's rim seen inside the ring.
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.contrast > b.contrast; });
    QList<Candidate> kept;
    for (const auto &c : found) {
        bool overlaps = false;
        for (const auto &k : kept) {
            const double distance = std::hypot(c.x - k.x, c.y - k.y);
            // Inside it, or a small fragment of its edge: a cell's size reads
            // a little small, so its rim can land just outside.
            if (distance < std::max(k.radius, 0.75 * c.radius) ||
                (distance < 1.35 * k.radius && c.radius < 0.4 * k.radius)) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps)
            kept << c;
    }
    return kept;
}
} // namespace

DetectResult detectBlobs(const QImage &image, const DetectOptions &options) {
    DetectResult result;
    if (image.isNull())
        return result;
    // Cells are found as round spots of their own size that stand out from
    // their own surroundings (a difference of Gaussians across sizes), not by
    // one gray level for the whole image. So uneven lighting, a dish rim or
    // hemocytometer grid lines, dim cells beside bright ones, and cells that
    // touch don't throw it off.
    // When even the smallest cell would still be several pixels across, the
    // work is done on a halved image, as often as that holds: a large scan
    // of large cells is found as well, and much sooner.
    Plane original = toPlane(image);
    double minRadius = std::max(1.5, std::sqrt(std::max(1, options.minArea) / M_PI));
    double maxRadius = std::min(original.width, original.height) / 10.0;
    if (options.maxArea > 0)
        maxRadius = std::min(maxRadius, std::sqrt(options.maxArea / M_PI));
    int reduced = 1;
    while (minRadius / 2 >= 3 && std::min(original.width, original.height) >= 256) {
        original = halve(original);
        minRadius /= 2;
        maxRadius /= 2;
        reduced *= 2;
    }
    maxRadius = std::max(maxRadius, minRadius * 1.5);
    const int shortSide = std::min(original.width, original.height);
    const Plane gray = shortSide >= 64 ? withoutLines(original, std::clamp(shortSide / 16 * 2 + 1, 41, 121)) : original;

    // Sizes step by k; larger ones are worked out on a halved image, which is
    // much faster and loses nothing at that size.
    const double k = 1.4;
    QList<double> sigmas;
    // One size more at the top is worked out only to compare against, so
    // anything larger than the largest size is left out.
    for (double sigma = minRadius / M_SQRT2; sigma <= maxRadius / M_SQRT2 * k * k; sigma *= k)
        sigmas << sigma;
    QList<Plane> pyramid{gray};
    QList<Level> levels;
    for (int i = 0; i + 1 < sigmas.size(); ++i) {
        int shift = 0;
        while (sigmas[i] / double(1 << (shift + 1)) >= 1.6 && pyramid[shift].width > 32 && pyramid[shift].height > 32) {
            if (shift + 1 >= pyramid.size())
                pyramid << halve(pyramid[shift]);
            ++shift;
        }
        const double s = double(1 << shift);
        // Halving already blurs by about half a pixel at that level.
        auto local = [&](double sigma) { return std::sqrt(std::max(0.01, sigma * sigma / (s * s) - 0.25 * (shift > 0))); };
        const Plane fine = blur(pyramid[shift], local(sigmas[i]));
        const Plane coarse = blur(pyramid[shift], local(sigmas[i + 1]));
        Level level;
        level.shift = shift;
        level.sigma = sigmas[i];
        level.response = Plane(fine.width, fine.height);
        for (size_t p = 0; p < fine.values.size(); ++p)
            level.response.values[p] = float((fine.values[p] - coarse.values[p]) / (k - 1));
        level.noise = noiseOf(level.response);
        levels << level;
    }

    const double threshold = options.threshold >= 0 ? std::min(options.threshold, 255) : -1;
    // Both kinds are looked for, because each place belongs to one object: a
    // weaker spot of one kind on a stronger spot of the other is its echo
    // (the bright background hugging a dark cell), not an object of its own.
    // The size limits are in image pixels; the work may be on a reduced copy.
    const double minArea = std::max(1, options.minArea) / double(reduced * reduced);
    const double maxArea = options.maxArea > 0 ? options.maxArea / double(reduced * reduced) : 0;
    const auto light = findPeaks(levels, pyramid, 1, threshold, minArea, maxArea);
    const auto dark = findPeaks(levels, pyramid, -1, threshold, minArea, maxArea);
    auto withoutEchoes = [](const QList<Candidate> &spots, const QList<Candidate> &other) {
        QList<Candidate> kept;
        for (const auto &c : spots) {
            bool echo = false;
            for (const auto &o : other)
                if (o.contrast > c.contrast && std::hypot(c.x - o.x, c.y - o.y) < 1.4 * o.radius) {
                    echo = true;
                    break;
                }
            if (!echo)
                kept << c;
        }
        return kept;
    };
    const auto lightSpots = withoutEchoes(light, dark), darkSpots = withoutEchoes(dark, light);
    if (options.polarity == DetectOptions::Auto) {
        // Keep the kind whose objects stand out more in total: dark cells on
        // a bright field, or bright nuclei and colonies.
        double lightScore = 0, darkScore = 0;
        for (const auto &c : lightSpots)
            lightScore += c.contrast;
        for (const auto &c : darkSpots)
            darkScore += c.contrast;
        result.dark = darkScore > lightScore;
    } else {
        result.dark = options.polarity == DetectOptions::Dark;
    }
    const QList<Candidate> found = result.dark ? darkSpots : lightSpots;

    // The threshold reported is the one used: the setting, or the automatic
    // level at the middle size.
    result.threshold = threshold >= 0 ? int(threshold)
                       : levels.isEmpty() ? 0
                                          : int(std::lround(std::max(2.0, noiseDeviations * levels[levels.size() / 2].noise) / discResponse));
    for (const auto &c : found) {
        if (c.x < 0 || c.y < 0 || c.x > gray.width || c.y > gray.height)
            continue;
        result.blobs << Blob{c.x * reduced, c.y * reduced, int(std::lround(c.area * reduced * reduced)), c.contrast};
    }
    return result;
}
