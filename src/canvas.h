#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QTransform>

#include "backend.h"

// The image with its markers, zoomed and panned. Painting goes through
// paintAnnotations, the same function the PNG export uses.
class CountCanvas : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *source READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY viewChanged)
    Q_PROPERTY(int highlighted READ highlighted WRITE setHighlighted NOTIFY viewChanged)
    Q_PROPERTY(QString previewShape READ previewShape WRITE setPreviewShape NOTIFY viewChanged)
    Q_PROPERTY(QRectF previewRect READ previewRect WRITE setPreviewRect NOTIFY viewChanged)
    Q_PROPERTY(double markerRadius READ markerRadius CONSTANT)

public:
    explicit CountCanvas(QQuickItem *parent = nullptr);

    Backend *backend() const { return m_backend; }
    void setBackend(Backend *backend);
    double zoom() const { return m_zoom; }
    QColor background() const { return m_background; }
    void setBackground(const QColor &color);
    int highlighted() const { return m_highlighted; }
    void setHighlighted(int index);
    QString previewShape() const { return m_previewShape; }
    void setPreviewShape(const QString &shape);
    QRectF previewRect() const { return m_previewRect; }
    void setPreviewRect(const QRectF &rect);
    double markerRadius() const { return 7; }

    Q_INVOKABLE void fit();
    // Keeps the image point under (x, y) where it is.
    Q_INVOKABLE void zoomAt(double x, double y, double factor);
    Q_INVOKABLE void zoomBy(double factor) { zoomAt(width() / 2, height() / 2, factor); }
    Q_INVOKABLE void panBy(double dx, double dy);
    Q_INVOKABLE QPointF toImage(double x, double y) const;
    Q_INVOKABLE bool insideImage(double x, double y) const;

    void paint(QPainter *painter) override;

signals:
    void backendChanged();
    void viewChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QTransform transform() const;

    QPointer<Backend> m_backend;
    QColor m_background = QColor("#0e0e10");
    double m_zoom = 1;
    QPointF m_offset; // where the image's origin sits in the view
    bool m_fitted = true;
    int m_highlighted = -1;
    QString m_previewShape;
    QRectF m_previewRect;
};
