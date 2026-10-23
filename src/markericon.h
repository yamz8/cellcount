#pragma once

#include <QColor>
#include <QQuickPaintedItem>

// A class's marker in the side panel, drawn by the same painter as the canvas.
class MarkerIcon : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString shape MEMBER m_shape NOTIFY changed)
    Q_PROPERTY(QColor color MEMBER m_color NOTIFY changed)

public:
    explicit MarkerIcon(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

signals:
    void changed();

private:
    QString m_shape = "circle";
    QColor m_color = Qt::white;
};
