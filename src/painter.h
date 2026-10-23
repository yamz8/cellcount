#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QTransform>

#include "session.h"

class QPainter;

// The one place markers, suggestions and the ROI are drawn, used by the canvas
// on screen and by the annotated PNG export, so the two always match.

// Class colors start at the Omarchy accent and step around the hue circle
// from it, keeping it readable on both dark and bright images.
QColor classColor(const QColor &accent, int index);
void paintMarker(QPainter *painter, const QPointF &center, double radius, const QString &shape,
                 const QColor &color, double opacity = 1);

struct Annotation {
    QTransform imageToView;
    double radius = 7;
    QColor accent;
    QList<QPointF> suggestions;
    int currentClass = 0;
    int highlighted = -1; // marker under the pointer
};

void paintAnnotations(QPainter *painter, const Session &session, const Annotation &annotation);
// The image with every marker and the ROI drawn on it, at full resolution.
QImage renderAnnotated(const QImage &image, const Session &session, const QColor &accent);
