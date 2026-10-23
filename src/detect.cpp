#include "detect.h"

#include <QFileInfo>
#include <QImageReader>
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

int otsuThreshold(const QImage &gray) {
    std::array<quint64, 256> histogram{};
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            ++histogram[line[x]];
    }
    const double total = double(gray.width()) * gray.height();
    double sum = 0;
    for (int i = 0; i < 256; ++i)
        sum += i * double(histogram[i]);
    double sumBackground = 0, weightBackground = 0, best = -1;
    int threshold = 127;
    for (int t = 0; t < 256; ++t) {
        weightBackground += histogram[t];
        if (weightBackground == 0)
            continue;
        const double weightForeground = total - weightBackground;
        if (weightForeground == 0)
            break;
        sumBackground += t * double(histogram[t]);
        const double meanBackground = sumBackground / weightBackground;
        const double meanForeground = (sum - sumBackground) / weightForeground;
        const double between = weightBackground * weightForeground * std::pow(meanBackground - meanForeground, 2);
        if (between > best) {
            best = between;
            threshold = t;
        }
    }
    return threshold;
}

DetectResult detectBlobs(const QImage &image, const DetectOptions &options) {
    DetectResult result;
    if (image.isNull())
        return result;
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const int width = gray.width(), height = gray.height();
    result.threshold = options.threshold >= 0 ? std::min(options.threshold, 255) : otsuThreshold(gray);

    // Objects are the minority side of the threshold unless told otherwise:
    // dark cells on a bright field, or bright spots on a dark one.
    qint64 below = 0;
    for (int y = 0; y < height; ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < width; ++x)
            below += line[x] <= result.threshold;
    }
    result.dark = options.polarity == DetectOptions::Dark   ? true
                : options.polarity == DetectOptions::Light ? false
                                                            : below * 2 <= qint64(width) * height;

    std::vector<uchar> mask(size_t(width) * height);
    for (int y = 0; y < height; ++y) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < width; ++x)
            mask[size_t(y) * width + x] = result.dark ? line[x] <= result.threshold : line[x] > result.threshold;
    }

    // Flood fill each blob with an explicit stack; recursion would overflow on
    // a large background region.
    std::vector<int> stack;
    for (int start = 0; start < width * height; ++start) {
        if (!mask[start])
            continue;
        mask[start] = 0;
        stack.push_back(start);
        qint64 sumX = 0, sumY = 0;
        int area = 0;
        bool touchesEdge = false;
        while (!stack.empty()) {
            const int index = stack.back();
            stack.pop_back();
            const int x = index % width, y = index / width;
            sumX += x;
            sumY += y;
            ++area;
            touchesEdge = touchesEdge || x == 0 || y == 0 || x == width - 1 || y == height - 1;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    const int next = ny * width + nx;
                    if (mask[next]) {
                        mask[next] = 0;
                        stack.push_back(next);
                    }
                }
        }
        if (area < options.minArea || (options.maxArea > 0 && area > options.maxArea))
            continue;
        // A region that fills most of the frame is background, not an object.
        if (touchesEdge && area > qint64(width) * height / 4)
            continue;
        result.blobs << Blob{sumX / double(area) + 0.5, sumY / double(area) + 0.5, area};
    }
    return result;
}
