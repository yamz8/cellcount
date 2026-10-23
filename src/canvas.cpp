#include "canvas.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

#include "backend.h"
#include "painter.h"

CountCanvas::CountCanvas(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void CountCanvas::setBackend(Backend *backend) {
    if (backend == m_backend)
        return;
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &Backend::imageChanged, this, &CountCanvas::fit);
        connect(m_backend, &Backend::changed, this, [this] { update(); });
        connect(m_backend, &Backend::suggestionsChanged, this, [this] { update(); });
    }
    emit backendChanged();
    fit();
}

void CountCanvas::setBackground(const QColor &color) {
    if (color == m_background)
        return;
    m_background = color;
    emit viewChanged();
    update();
}

void CountCanvas::setHighlighted(int index) {
    if (index == m_highlighted)
        return;
    m_highlighted = index;
    emit viewChanged();
    update();
}

void CountCanvas::setPreviewShape(const QString &shape) {
    if (shape == m_previewShape)
        return;
    m_previewShape = shape;
    emit viewChanged();
    update();
}

void CountCanvas::setPreviewRect(const QRectF &rect) {
    if (rect == m_previewRect)
        return;
    m_previewRect = rect;
    emit viewChanged();
    update();
}

QTransform CountCanvas::transform() const {
    return QTransform(m_zoom, 0, 0, m_zoom, m_offset.x(), m_offset.y());
}

void CountCanvas::fit() {
    m_fitted = true;
    if (!m_backend || !m_backend->hasImage() || width() <= 0 || height() <= 0) {
        update();
        return;
    }
    const QSizeF image = m_backend->imageSize();
    const double margin = 24;
    m_zoom = std::min((width() - 2 * margin) / image.width(), (height() - 2 * margin) / image.height());
    m_zoom = std::max(m_zoom, 0.01);
    m_offset = QPointF((width() - image.width() * m_zoom) / 2, (height() - image.height() * m_zoom) / 2);
    emit viewChanged();
    update();
}

void CountCanvas::zoomAt(double x, double y, double factor) {
    if (!m_backend || !m_backend->hasImage())
        return;
    const double zoom = std::clamp(m_zoom * factor, 0.02, 64.0);
    const QPointF anchor = toImage(x, y);
    m_zoom = zoom;
    m_offset = QPointF(x - anchor.x() * m_zoom, y - anchor.y() * m_zoom);
    m_fitted = false;
    emit viewChanged();
    update();
}

void CountCanvas::panBy(double dx, double dy) {
    m_offset += QPointF(dx, dy);
    m_fitted = false;
    update();
}

QPointF CountCanvas::toImage(double x, double y) const {
    return QPointF((x - m_offset.x()) / m_zoom, (y - m_offset.y()) / m_zoom);
}

bool CountCanvas::insideImage(double x, double y) const {
    if (!m_backend || !m_backend->hasImage())
        return false;
    const QPointF point = toImage(x, y);
    return QRectF(QPointF(0, 0), QSizeF(m_backend->imageSize())).contains(point);
}

void CountCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (m_fitted)
        fit();
}

void CountCanvas::paint(QPainter *painter) {
    painter->fillRect(QRectF(0, 0, width(), height()), m_background);
    if (!m_backend || !m_backend->hasImage())
        return;
    const QImage &image = m_backend->image();
    const QTransform t = transform();

    // Draw only the part of the image that's in view, so a large scan pans
    // smoothly. Zoomed in, pixels stay crisp so cells can be told apart.
    const QRectF visible = t.inverted().mapRect(QRectF(0, 0, width(), height()))
                               .intersected(QRectF(QPointF(0, 0), QSizeF(image.size())));
    if (!visible.isEmpty()) {
        const QRect source = visible.toAlignedRect();
        painter->setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 2);
        painter->drawImage(t.mapRect(QRectF(source)), image, source);
    }

    Annotation annotation;
    annotation.imageToView = t;
    annotation.radius = markerRadius();
    annotation.accent = m_backend->accent();
    annotation.suggestions = m_backend->suggestions();
    annotation.currentClass = m_backend->currentClass();
    annotation.highlighted = m_highlighted;
    paintAnnotations(painter, m_backend->session(), annotation);

    if (!m_previewShape.isEmpty()) {
        painter->setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const QRectF r = m_previewRect;
        if (m_previewShape == "circle") {
            const double radius = std::hypot(r.width(), r.height());
            path.addEllipse(r.topLeft(), radius, radius);
        } else {
            path.addRect(r.normalized());
        }
        const QPainterPath view = t.map(path);
        painter->setPen(QPen(QColor(0, 0, 0, 170), 4.5));
        painter->drawPath(view);
        painter->setPen(QPen(m_backend->accent(), 2, Qt::DashLine));
        painter->drawPath(view);
    }
}
