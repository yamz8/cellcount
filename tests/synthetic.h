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
