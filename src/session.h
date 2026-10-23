#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

struct CountClass {
    QString name;
    QString shape;
    bool operator==(const CountClass &) const = default;
};

struct Marker {
    double x = 0;
    double y = 0;
    int cls = 0;
    bool operator==(const Marker &) const = default;
};

// The region of interest. A circle is stored as its bounding square, so both
// shapes share one rectangle in image pixels.
struct Roi {
    QString shape; // "", "rect" or "circle"
    QRectF rect;

    bool isSet() const { return !shape.isEmpty() && rect.width() > 0 && rect.height() > 0; }
    bool contains(const QPointF &point) const;
    double area() const;
    bool operator==(const Roi &) const = default;
};

struct Problem {
    bool error = true;
    QString message;
};

// One counting session: the image it belongs to and everything placed on it.
// Pure data and arithmetic, so the window, the command line and the tests all
// count the same way.
struct Session {
    static constexpr int maxClasses = 9;
    static constexpr int version = 1;

    QString imagePath; // absolute
    QSize imageSize;
    QList<CountClass> classes;
    QList<Marker> markers;
    Roi roi;
    double pixelsPerUm = 0; // 0 means no scale is set
    int liveClass = 0;      // -1 when viability doesn't apply
    int deadClass = 1;
    double dilution = 1;
    int squares = 0;
    // A large square of a Neubauer hemocytometer: 1 mm × 1 mm × 0.1 mm.
    double squareVolumeMl = 1e-4;

    bool operator==(const Session &) const = default;

    static Session withDefaults(const QStringList &names);
    static QStringList shapes();
    static QString sessionPathFor(const QString &imagePath);

    bool counted(const Marker &marker) const;
    QList<int> classCounts() const;
    int total() const;
    // Percent of the counted total, or NaN when nothing is counted.
    double percent(int cls) const;
    // Live / (Live + Dead) in percent, or NaN when it can't be worked out.
    double viability() const;
    // The ROI's area, or the whole image's when there is no ROI.
    double areaPx() const;
    // NaN without a scale.
    double areaUm2() const;
    // Cells per mL from a hemocytometer count, or NaN without squares.
    double concentration(int count) const;

    QJsonObject toJson(const QString &sessionDirectory) const;
    // Reads leniently and reports what's wrong; returns false on any error.
    static bool fromJson(const QJsonObject &json, const QString &sessionDirectory, Session *out,
                         QList<Problem> *problems);
    static bool load(const QString &path, Session *out, QList<Problem> *problems);
    bool save(const QString &path, QString *error) const;

    QString markersCsv() const;
    QString summaryCsv() const;
    QJsonObject summaryJson() const;
};

// Writes beside the target and renames, so a crash never leaves half a file.
bool writeAtomically(const QString &path, const QByteArray &data, QString *error);
