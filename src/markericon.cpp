#include "markericon.h"

#include <QPainter>

#include "painter.h"

MarkerIcon::MarkerIcon(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &MarkerIcon::changed, this, [this] { update(); });
}

void MarkerIcon::paint(QPainter *painter) {
    painter->setRenderHint(QPainter::Antialiasing);
    paintMarker(painter, QPointF(width() / 2, height() / 2), std::min(width(), height()) / 2 - 3, m_shape, m_color);
}
