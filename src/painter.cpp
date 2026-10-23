#include "painter.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>

QColor classColor(const QColor &accent, int index) {
    if (index == 0)
        return accent;
    // Step by the golden angle, so neighbouring classes never land close
    // together however many there are. A gray accent has no hue to start from.
    const double startHue = accent.hslHueF() < 0 ? 0.58 : double(accent.hslHueF());
    const double hue = std::fmod(startHue + index * 0.381966, 1.0);
    const double saturation = std::clamp(double(accent.hslSaturationF()), 0.6, 0.9);
    const double lightness = std::clamp(double(accent.lightnessF()), 0.55, 0.68);
    return QColor::fromHslF(float(hue), float(saturation), float(lightness));
}

namespace {
QPainterPath polygon(const QPointF &center, double radius, int corners, double rotation) {
    QPainterPath path;
    for (int i = 0; i < corners; ++i) {
        const double angle = rotation + i * 2 * M_PI / corners;
        const QPointF point(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
        i == 0 ? path.moveTo(point) : path.lineTo(point);
    }
    path.closeSubpath();
    return path;
}

QPainterPath shapePath(const QString &shape, const QPointF &c, double r) {
    QPainterPath path;
    if (shape == "square") {
        path.addRect(QRectF(c.x() - r * 0.8, c.y() - r * 0.8, r * 1.6, r * 1.6));
    } else if (shape == "triangle") {
        path = polygon(c + QPointF(0, r * 0.15), r * 1.1, 3, -M_PI / 2);
    } else if (shape == "diamond") {
        path = polygon(c, r * 1.1, 4, -M_PI / 2);
    } else if (shape == "plus") {
        path.moveTo(c.x() - r, c.y());
        path.lineTo(c.x() + r, c.y());
        path.moveTo(c.x(), c.y() - r);
        path.lineTo(c.x(), c.y() + r);
    } else if (shape == "cross") {
        const double d = r * 0.8;
        path.moveTo(c.x() - d, c.y() - d);
        path.lineTo(c.x() + d, c.y() + d);
        path.moveTo(c.x() + d, c.y() - d);
        path.lineTo(c.x() - d, c.y() + d);
    } else if (shape == "star") {
        for (int i = 0; i < 10; ++i) {
            const double angle = -M_PI / 2 + i * M_PI / 5;
            const double radius = i % 2 ? r * 0.5 : r * 1.15;
            const QPointF point(c.x() + radius * std::cos(angle), c.y() + radius * std::sin(angle));
            i == 0 ? path.moveTo(point) : path.lineTo(point);
        }
        path.closeSubpath();
    } else if (shape == "hexagon") {
        path = polygon(c, r, 6, 0);
    } else if (shape == "chevron") {
        path.moveTo(c.x() - r, c.y() - r * 0.5);
        path.lineTo(c.x(), c.y() + r * 0.6);
        path.lineTo(c.x() + r, c.y() - r * 0.5);
    } else {
        path.addEllipse(c, r, r);
    }
    return path;
}
} // namespace

void paintMarker(QPainter *painter, const QPointF &center, double radius, const QString &shape,
                 const QColor &color, double opacity) {
    const QPainterPath path = shapePath(shape, center, radius);
    const double width = std::max(1.5, radius / 3.5);
    painter->save();
    painter->setOpacity(opacity);
    painter->setBrush(Qt::NoBrush);
    // A dark halo under the colored stroke keeps markers visible on bright
    // brightfield images as well as dark fluorescence ones.
    painter->setPen(QPen(QColor(0, 0, 0, 170), width + 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(path);
    painter->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(path);
    painter->restore();
}

void paintAnnotations(QPainter *painter, const Session &session, const Annotation &annotation) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QTransform &t = annotation.imageToView;
    const double r = annotation.radius;

    if (session.roi.isSet()) {
        // Shade everything outside the ROI, then outline it.
        QPainterPath roi;
        if (session.roi.shape == "circle")
            roi.addEllipse(session.roi.rect);
        else
            roi.addRect(session.roi.rect);
        const QPainterPath outline = t.map(roi);
        QPainterPath image;
        image.addRect(QRectF(QPointF(0, 0), QSizeF(session.imageSize)));
        painter->fillPath(t.map(image).subtracted(outline), QColor(0, 0, 0, 110));
        painter->setPen(QPen(QColor(0, 0, 0, 170), std::max(2.0, r / 3.5) + 2.5));
        painter->drawPath(outline);
        painter->setPen(QPen(annotation.accent, std::max(2.0, r / 3.5), Qt::DashLine));
        painter->drawPath(outline);
    }

    const QColor suggestion = classColor(annotation.accent, annotation.currentClass);
    for (const auto &point : annotation.suggestions) {
        const QPointF c = t.map(point);
        painter->setPen(QPen(QColor(0, 0, 0, 150), std::max(1.5, r / 4) + 2));
        painter->drawEllipse(c, r * 1.2, r * 1.2);
        painter->setPen(QPen(suggestion, std::max(1.5, r / 4), Qt::DotLine));
        painter->drawEllipse(c, r * 1.2, r * 1.2);
    }

    for (int i = 0; i < session.markers.size(); ++i) {
        const Marker &marker = session.markers[i];
        if (marker.cls < 0 || marker.cls >= session.classes.size())
            continue;
        const QPointF c = t.map(QPointF(marker.x, marker.y));
        if (i == annotation.highlighted) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, 90));
            painter->drawEllipse(c, r * 1.7, r * 1.7);
        }
        // Markers outside the ROI stay visible but faded, as they aren't counted.
        paintMarker(painter, c, r, session.classes[marker.cls].shape, classColor(annotation.accent, marker.cls),
                    session.counted(marker) ? 1 : 0.35);
    }
    painter->restore();
}

QImage renderAnnotated(const QImage &image, const Session &session, const QColor &accent) {
    QImage out = image.convertToFormat(QImage::Format_RGB32);
    QPainter painter(&out);
    Annotation annotation;
    annotation.accent = accent;
    annotation.radius = std::max(6.0, std::min(out.width(), out.height()) / 100.0);
    paintAnnotations(&painter, session, annotation);
    return out;
}
