#pragma once

// Made-up microscope fields with cells at known places, for the tests and the
// README screenshot. No real lab data is used anywhere.

#include <QImage>
#include <QList>
#include <QPainter>
#include <QPointF>
#include <QRandomGenerator>
#include <QRadialGradient>
#include <cmath>

struct SyntheticCell {
    QPointF center;
    double radius;
    bool dead;
};

// Dark round cells on a bright, slightly uneven field, laid out on a jittered
// grid so no two touch. Every fifth cell is "dead": darker, with no halo.
inline QImage syntheticField(int width, int height, QList<SyntheticCell> *cells, bool brightCells = false,
                             quint32 seed = 7, int spacing = 44) {
    QRandomGenerator random(seed);
    QImage image(width, height, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient field(0, 0, width, height);
    field.setColorAt(0, brightCells ? QColor(18, 20, 26) : QColor(214, 210, 200));
    field.setColorAt(1, brightCells ? QColor(26, 24, 30) : QColor(196, 194, 188));
    painter.fillRect(image.rect(), field);

    int n = 0;
    for (int y = spacing; y < height - spacing / 2; y += spacing)
        for (int x = spacing; x < width - spacing / 2; x += spacing) {
            const QPointF center(x + random.bounded(spacing / 3) - spacing / 6,
                                 y + random.bounded(spacing / 3) - spacing / 6);
            const double radius = 7 + random.bounded(5);
            const bool dead = ++n % 5 == 0;
            if (cells)
                *cells << SyntheticCell{center, radius, dead};
            QRadialGradient body(center, radius);
            if (brightCells) {
                body.setColorAt(0, dead ? QColor(120, 70, 70) : QColor(150, 240, 160));
                body.setColorAt(1, dead ? QColor(90, 50, 50) : QColor(80, 170, 100));
            } else {
                body.setColorAt(0, dead ? QColor(40, 42, 70) : QColor(120, 110, 100));
                body.setColorAt(0.8, dead ? QColor(30, 30, 55) : QColor(80, 72, 66));
                body.setColorAt(1, dead ? QColor(25, 25, 45) : QColor(60, 54, 50));
            }
            if (!brightCells && !dead) {
                painter.setPen(QPen(QColor(245, 244, 238), 2));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(center, radius + 2, radius + 2);
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(body);
            painter.drawEllipse(center, radius, radius);
        }
    painter.end();

    // Sensor noise, so thresholding has something to cope with.
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int noise = random.bounded(13) - 6;
            const QRgb p = line[x];
            line[x] = qRgb(qBound(0, qRed(p) + noise, 255), qBound(0, qGreen(p) + noise, 255),
                           qBound(0, qBlue(p) + noise, 255));
        }
    }
    return image;
}

// One kind of image per counting job, each with the true cells.
namespace synthetic {

inline double uniform(QRandomGenerator &random, double from, double to) { return from + (to - from) * random.generateDouble(); }

inline double gaussian(QRandomGenerator &random) {
    const double u = 1 - random.generateDouble(), v = random.generateDouble();
    return std::sqrt(-2 * std::log(u)) * std::cos(2 * M_PI * v);
}

// Up to count circles that don't overlap, plus a few placed right against
// another, inside the area allowed.
template <typename Allowed>
QList<SyntheticCell> place(QRandomGenerator &random, int width, int height, int count, double smallest,
                           double largest, int touching, Allowed allowed) {
    QList<SyntheticCell> cells;
    for (int tries = 0; cells.size() < count && tries < 100000; ++tries) {
        const double r = uniform(random, smallest, largest);
        const QPointF c(uniform(random, r + 2, width - r - 2), uniform(random, r + 2, height - r - 2));
        bool free = allowed(c);
        for (const auto &cell : cells)
            free = free && std::hypot(cell.center.x() - c.x(), cell.center.y() - c.y()) > cell.radius + r + 4;
        if (free)
            cells << SyntheticCell{c, r, false};
    }
    const int placed = cells.size();
    for (int i = 0; i < touching && i < placed; ++i) {
        const auto a = cells[i];
        const double angle = uniform(random, 0, 2 * M_PI), r = uniform(random, smallest, largest);
        const QPointF c = a.center + QPointF(std::cos(angle), std::sin(angle)) * (a.radius + r) * 0.95;
        bool free = allowed(c) && c.x() > r + 2 && c.y() > r + 2 && c.x() < width - r - 2 && c.y() < height - r - 2;
        for (int j = 0; j < cells.size(); ++j)
            free = free && (j == i || std::hypot(cells[j].center.x() - c.x(), cells[j].center.y() - c.y()) > cells[j].radius + r + 3);
        if (free)
            cells << SyntheticCell{c, r, false};
    }
    return cells;
}

inline void addNoise(QImage &image, QRandomGenerator &random, double sigma) {
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int n = int(std::lround(gaussian(random) * sigma));
            line[x] = qRgb(qBound(0, qRed(line[x]) + n, 255), qBound(0, qGreen(line[x]) + n, 255),
                           qBound(0, qBlue(line[x]) + n, 255));
        }
    }
}

// Trypan blue in a hemocytometer: a grid of single and triple lines, clear
// live cells with a dark rim, and dead cells filled dark ("dead" set).
inline QImage hemocytometer(QList<SyntheticCell> *cells, quint32 seed = 1) {
    QRandomGenerator random(seed);
    const int w = 900, h = 700;
    *cells = place(random, w, h, 45, 9, 13, 4, [](QPointF) { return true; });
    for (int i = 0; i < cells->size(); ++i)
        (*cells)[i].dead = i % 6 == 0;
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(QColor(222, 222, 214));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    for (int g = 0; g <= std::max(w, h); g += 75) {
        const bool major = g % 300 == 0;
        p.setPen(QPen(QColor(150, 150, 145), major ? 2 : 1));
        for (int offset : major ? QList<int>{-5, 0, 5} : QList<int>{0}) {
            p.drawLine(g + offset, 0, g + offset, h);
            p.drawLine(0, g + offset, w, g + offset);
        }
    }
    for (const auto &c : *cells) {
        if (c.dead) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(45, 60, 130));
        } else {
            p.setPen(QPen(QColor(70, 70, 66), 2.5));
            p.setBrush(QColor(238, 238, 232));
        }
        p.drawEllipse(c.center, c.radius, c.radius);
    }
    p.end();
    addNoise(image, random, 4);
    return image;
}

// Colonies on an agar plate: dark surround, amber agar with a bright rim,
// cream colonies from 5 to 22 px in radius, some touching.
inline QImage plate(QList<SyntheticCell> *cells, quint32 seed = 2) {
    QRandomGenerator random(seed);
    const int size = 800;
    const QPointF center(size / 2.0, size / 2.0);
    const double radius = 350;
    *cells = place(random, size, size, 40, 5, 22, 5,
                   [&](QPointF c) { return std::hypot(c.x() - center.x(), c.y() - center.y()) < radius - 30; });
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(QColor(25, 25, 28));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(235, 230, 215), 6));
    p.setBrush(QColor(185, 138, 72));
    p.drawEllipse(center, radius, radius);
    p.setPen(Qt::NoPen);
    for (const auto &c : *cells) {
        p.setBrush(QColor(240, 232, 205));
        p.drawEllipse(c.center, c.radius, c.radius);
    }
    p.end();
    addNoise(image, random, 3);
    return image;
}

// Fluorescent nuclei as a 16-bit image: an offset background, soft-edged
// nuclei of uneven brightness (a fifth of them dim), some touching.
inline QImage nuclei16(QList<SyntheticCell> *cells, quint32 seed = 3) {
    QRandomGenerator random(seed);
    const int size = 600;
    *cells = place(random, size, size, 60, 7, 12, 6, [](QPointF) { return true; });
    std::vector<float> values(size_t(size) * size, 400);
    for (const auto &c : *cells) {
        const float peak = float(uniform(random, 900, 4000)) * (random.bounded(5) == 0 ? 0.35f : 1.f);
        for (int y = std::max(0, int(c.center.y() - c.radius - 3)); y < std::min(size, int(c.center.y() + c.radius + 4)); ++y)
            for (int x = std::max(0, int(c.center.x() - c.radius - 3)); x < std::min(size, int(c.center.x() + c.radius + 4)); ++x) {
                const double d = std::hypot(x + 0.5 - c.center.x(), y + 0.5 - c.center.y()) / c.radius;
                values[size_t(y) * size + x] += peak * float(std::exp(-std::pow(d / 0.9, 6)));
            }
    }
    QImage image(size, size, QImage::Format_Grayscale16);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            reinterpret_cast<quint16 *>(image.scanLine(y))[x] =
                quint16(qBound(0L, std::lround(values[size_t(y) * size + x] + gaussian(random) * 60), 65535L));
    return image;
}

// Dark cells under uneven light: the corners get less than half the light
// of the middle.
inline QImage vignetted(QList<SyntheticCell> *cells, quint32 seed = 4) {
    QRandomGenerator random(seed);
    const int w = 900, h = 700;
    *cells = place(random, w, h, 60, 8, 12, 0, [](QPointF) { return true; });
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double d = std::hypot(x - w / 2.0, y - h / 2.0) / std::hypot(w / 2.0, h / 2.0);
            double v = 215 - 120 * d * d;
            for (const auto &c : *cells)
                if (std::hypot(x + 0.5 - c.center.x(), y + 0.5 - c.center.y()) < c.radius)
                    v *= 0.45;
            const int g = qBound(0, int(std::lround(v)), 255);
            reinterpret_cast<QRgb *>(image.scanLine(y))[x] = qRgb(g, g, g);
        }
    addNoise(image, random, 4);
    return image;
}

// A crowded field where a fifth of the cells touch a neighbour.
inline QImage crowded(QList<SyntheticCell> *cells, quint32 seed = 5) {
    QRandomGenerator random(seed);
    const int w = 600, h = 450;
    *cells = place(random, w, h, 110, 9, 11, 25, [](QPointF) { return true; });
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(QColor(210, 210, 210));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(70, 70, 70));
    for (const auto &c : *cells)
        p.drawEllipse(c.center, c.radius, c.radius);
    p.end();
    addNoise(image, random, 5);
    return image;
}

} // namespace synthetic
