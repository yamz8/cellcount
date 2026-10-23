#include "session.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtMath>
#include <cmath>
#include <limits>

namespace {
constexpr double notANumber = std::numeric_limits<double>::quiet_NaN();

QString csvField(const QString &text) {
    if (!text.contains(',') && !text.contains('"') && !text.contains('\n'))
        return text;
    return '"' + QString(text).replace('"', "\"\"") + '"';
}

// Plain C-locale numbers, so files read the same in every language.
QString number(double value, int decimals = 4) {
    if (std::isnan(value))
        return {};
    QString text = QString::number(value, 'f', decimals);
    if (text.contains('.')) {
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
    }
    return text;
}

QJsonValue jsonNumber(double value) { return std::isnan(value) ? QJsonValue() : QJsonValue(value); }
} // namespace

bool Roi::contains(const QPointF &point) const {
    if (!isSet())
        return true;
    if (shape == "circle") {
        const QPointF center = rect.center();
        const double radius = rect.width() / 2;
        const double dx = point.x() - center.x(), dy = point.y() - center.y();
        return dx * dx + dy * dy <= radius * radius;
    }
    return point.x() >= rect.left() && point.x() <= rect.right() && point.y() >= rect.top() &&
           point.y() <= rect.bottom();
}

double Roi::area() const {
    if (!isSet())
        return 0;
    if (shape == "circle")
        return M_PI * std::pow(rect.width() / 2, 2);
    return rect.width() * rect.height();
}

Session Session::withDefaults(const QStringList &names) {
    Session session;
    const auto all = shapes();
    for (int i = 0; i < names.size() && i < maxClasses; ++i)
        session.classes << CountClass{names[i], all[i]};
    return session;
}

// One shape per class, so the classes read apart without color.
QStringList Session::shapes() {
    return {"circle", "square", "triangle", "diamond", "plus", "cross", "star", "hexagon", "chevron"};
}

QString Session::sessionPathFor(const QString &imagePath) { return imagePath + ".cellcount"; }

bool Session::counted(const Marker &marker) const { return roi.contains({marker.x, marker.y}); }

QList<int> Session::classCounts() const {
    QList<int> counts(classes.size(), 0);
    for (const auto &marker : markers)
        if (marker.cls >= 0 && marker.cls < counts.size() && counted(marker))
            ++counts[marker.cls];
    return counts;
}

int Session::total() const {
    int sum = 0;
    for (int count : classCounts())
        sum += count;
    return sum;
}

double Session::percent(int cls) const {
    const auto counts = classCounts();
    const int sum = total();
    if (sum == 0 || cls < 0 || cls >= counts.size())
        return notANumber;
    return 100.0 * counts[cls] / sum;
}

double Session::viability() const {
    const auto counts = classCounts();
    if (liveClass < 0 || deadClass < 0 || liveClass >= counts.size() || deadClass >= counts.size() ||
        liveClass == deadClass)
        return notANumber;
    const int both = counts[liveClass] + counts[deadClass];
    return both == 0 ? notANumber : 100.0 * counts[liveClass] / both;
}

double Session::areaPx() const {
    if (roi.isSet())
        return roi.area();
    return double(imageSize.width()) * imageSize.height();
}

double Session::areaUm2() const { return pixelsPerUm > 0 ? areaPx() / (pixelsPerUm * pixelsPerUm) : notANumber; }

double Session::concentration(int count) const {
    if (squares <= 0 || squareVolumeMl <= 0)
        return notANumber;
    return count / double(squares) * dilution / squareVolumeMl;
}

QJsonObject Session::toJson(const QString &sessionDirectory) const {
    QJsonArray classList, markerList;
    for (const auto &cls : classes)
        classList << QJsonObject{{"name", cls.name}, {"shape", cls.shape}};
    for (const auto &marker : markers)
        markerList << QJsonObject{{"x", marker.x}, {"y", marker.y}, {"class", marker.cls}};
    // The image is stored relative to the session, so the pair can move together.
    const QString image = sessionDirectory.isEmpty() ? imagePath : QDir(sessionDirectory).relativeFilePath(imagePath);
    QJsonObject json{
        {"cellcount", version},
        {"image", image},
        {"imageSize", QJsonArray{imageSize.width(), imageSize.height()}},
        {"pixelsPerUm", pixelsPerUm},
        {"classes", classList},
        {"viability", QJsonObject{{"live", liveClass}, {"dead", deadClass}}},
        {"hemocytometer",
         QJsonObject{{"dilution", dilution}, {"squares", squares}, {"squareVolumeMl", squareVolumeMl}}},
        {"roi", roi.isSet() ? QJsonValue(QJsonObject{{"shape", roi.shape},
                                                     {"x", roi.rect.x()},
                                                     {"y", roi.rect.y()},
                                                     {"width", roi.rect.width()},
                                                     {"height", roi.rect.height()}})
                            : QJsonValue()},
        {"markers", markerList},
    };
    return json;
}

bool Session::fromJson(const QJsonObject &json, const QString &sessionDirectory, Session *out,
                       QList<Problem> *problems) {
    QList<Problem> found;
    auto error = [&](const QString &message) { found << Problem{true, message}; };
    auto warning = [&](const QString &message) { found << Problem{false, message}; };
    Session session;

    if (json.value("cellcount").toInt() != version)
        error(QString("Not a cellcount session (expected \"cellcount\": %1)").arg(version));

    const QString image = json.value("image").toString();
    if (image.isEmpty())
        error("No image is named");
    else
        session.imagePath = QDir::cleanPath(QDir(sessionDirectory).absoluteFilePath(image));
    const QJsonArray size = json.value("imageSize").toArray();
    session.imageSize = QSize(size.at(0).toInt(), size.at(1).toInt());

    session.pixelsPerUm = json.value("pixelsPerUm").toDouble();
    if (session.pixelsPerUm < 0) {
        error("pixelsPerUm must not be negative");
        session.pixelsPerUm = 0;
    }

    const QJsonArray classes = json.value("classes").toArray();
    if (classes.isEmpty())
        error("There are no classes");
    if (classes.size() > maxClasses)
        error(QString("There are %1 classes; at most %2 are allowed").arg(classes.size()).arg(maxClasses));
    for (int i = 0; i < classes.size() && i < maxClasses; ++i) {
        const QJsonObject cls = classes[i].toObject();
        QString shape = cls.value("shape").toString();
        if (!shapes().contains(shape)) {
            warning(QString("Class %1 has unknown shape \"%2\"; using %3").arg(i + 1).arg(shape, shapes()[i]));
            shape = shapes()[i];
        }
        const QString name = cls.value("name").toString().trimmed();
        if (name.isEmpty())
            warning(QString("Class %1 has no name").arg(i + 1));
        session.classes << CountClass{name.isEmpty() ? QString("Class %1").arg(i + 1) : name, shape};
    }

    const QJsonObject viability = json.value("viability").toObject();
    session.liveClass = viability.value("live").toInt(-1);
    session.deadClass = viability.value("dead").toInt(-1);
    if (session.liveClass >= session.classes.size() || session.deadClass >= session.classes.size()) {
        warning("Viability names a class that doesn't exist; viability is off");
        session.liveClass = session.deadClass = -1;
    }

    const QJsonObject hemocytometer = json.value("hemocytometer").toObject();
    session.dilution = hemocytometer.value("dilution").toDouble(1);
    session.squares = hemocytometer.value("squares").toInt(0);
    session.squareVolumeMl = hemocytometer.value("squareVolumeMl").toDouble(1e-4);
    if (session.dilution <= 0)
        error("The dilution factor must be greater than 0");
    if (session.squares < 0)
        error("The number of squares must not be negative");
    if (session.squareVolumeMl <= 0)
        error("squareVolumeMl must be greater than 0");

    const QJsonValue roi = json.value("roi");
    if (roi.isObject()) {
        const QJsonObject r = roi.toObject();
        session.roi.shape = r.value("shape").toString();
        session.roi.rect = QRectF(r.value("x").toDouble(), r.value("y").toDouble(), r.value("width").toDouble(),
                                  r.value("height").toDouble());
        if (session.roi.shape != "rect" && session.roi.shape != "circle")
            error("The ROI shape must be \"rect\" or \"circle\"");
        else if (!session.roi.isSet())
            error("The ROI has no area");
        else if (session.roi.shape == "circle" && !qFuzzyCompare(session.roi.rect.width(), session.roi.rect.height()))
            error("A circular ROI must have equal width and height");
    } else if (!roi.isNull() && !roi.isUndefined()) {
        error("The ROI must be an object or null");
    }

    const QJsonArray markers = json.value("markers").toArray();
    int outside = 0;
    for (int i = 0; i < markers.size(); ++i) {
        const QJsonObject m = markers[i].toObject();
        const Marker marker{m.value("x").toDouble(), m.value("y").toDouble(), m.value("class").toInt(-1)};
        if (marker.cls < 0 || marker.cls >= session.classes.size()) {
            error(QString("Marker %1 has class %2, which doesn't exist").arg(i + 1).arg(m.value("class").toVariant().toString()));
            continue;
        }
        if (session.imageSize.isValid() &&
            (marker.x < 0 || marker.y < 0 || marker.x > session.imageSize.width() || marker.y > session.imageSize.height()))
            ++outside;
        session.markers << marker;
    }
    if (outside)
        warning(QString("%1 markers lie outside the image").arg(outside));

    bool ok = true;
    for (const auto &problem : found)
        ok = ok && !problem.error;
    if (problems)
        *problems << found;
    if (out)
        *out = session;
    return ok;
}

bool Session::load(const QString &path, Session *out, QList<Problem> *problems) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (problems)
            *problems << Problem{true, file.errorString()};
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        if (problems)
            *problems << Problem{true, parseError.error != QJsonParseError::NoError
                                           ? QString("Invalid JSON at offset %1: %2").arg(parseError.offset).arg(parseError.errorString())
                                           : QString("The session must be a JSON object")};
        return false;
    }
    return fromJson(document.object(), QFileInfo(path).absolutePath(), out, problems);
}

bool Session::save(const QString &path, QString *error) const {
    const QByteArray data = QJsonDocument(toJson(QFileInfo(path).absolutePath())).toJson(QJsonDocument::Indented);
    return writeAtomically(path, data, error);
}

bool writeAtomically(const QString &path, const QByteArray &data, QString *error) {
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
        return true;
    if (error)
        *error = file.errorString();
    return false;
}

QString Session::markersCsv() const {
    QString csv = "x,y,class,class_name,counted\n";
    for (const auto &marker : markers) {
        const QString name = marker.cls >= 0 && marker.cls < classes.size() ? classes[marker.cls].name : QString();
        csv += QString("%1,%2,%3,%4,%5\n")
                   .arg(number(marker.x, 2), number(marker.y, 2))
                   .arg(marker.cls + 1)
                   .arg(csvField(name), counted(marker) ? "yes" : "no");
    }
    return csv;
}

// A long table, one measurement per row, which spreadsheets and scripts both read.
QString Session::summaryCsv() const {
    QString csv = "metric,class,value,unit\n";
    auto row = [&](const QString &metric, const QString &cls, const QString &value, const QString &unit) {
        csv += QString("%1,%2,%3,%4\n").arg(metric, csvField(cls), value, unit);
    };
    const auto counts = classCounts();
    for (int i = 0; i < classes.size(); ++i) {
        row("count", classes[i].name, QString::number(counts[i]), "");
        row("percent", classes[i].name, number(percent(i), 2), "%");
        if (squares > 0)
            row("concentration", classes[i].name, number(concentration(counts[i]), 0), "cells/mL");
    }
    row("count", "Total", QString::number(total()), "");
    if (squares > 0)
        row("concentration", "Total", number(concentration(total()), 0), "cells/mL");
    if (!std::isnan(viability()))
        row("viability", "", number(viability(), 2), "%");
    row(roi.isSet() ? "roi_area" : "image_area", "", number(areaPx(), 1), "px2");
    if (pixelsPerUm > 0)
        row(roi.isSet() ? "roi_area" : "image_area", "", number(areaUm2(), 2), "um2");
    return csv;
}

QJsonObject Session::summaryJson() const {
    const auto counts = classCounts();
    QJsonArray list;
    for (int i = 0; i < classes.size(); ++i)
        list << QJsonObject{{"class", i + 1},
                            {"name", classes[i].name},
                            {"count", counts[i]},
                            {"percent", jsonNumber(percent(i))},
                            {"cellsPerMl", jsonNumber(concentration(counts[i]))}};
    return {
        {"image", imagePath},
        {"classes", list},
        {"total", total()},
        {"viability", jsonNumber(viability())},
        {"roi", roi.isSet() ? QJsonValue(roi.shape) : QJsonValue()},
        {"areaPx2", areaPx()},
        {"areaUm2", jsonNumber(areaUm2())},
        {"areaMm2", jsonNumber(areaUm2() / 1e6)},
        {"cellsPerMl", jsonNumber(concentration(total()))},
        {"markers", markers.size()},
    };
}
