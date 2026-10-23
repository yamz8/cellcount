#include "backend.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QtConcurrentRun>
#include <cmath>

#include "filepicker.h"
#include "painter.h"
#include "theme.h"

namespace {
constexpr int undoLimit = 500;

double distanceSquared(double ax, double ay, double bx, double by) {
    return (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
}
} // namespace

Backend::Backend(Theme *theme, FilePicker *filePicker, QObject *parent)
    : QObject(parent), m_theme(theme), m_filePicker(filePicker) {
    if (m_theme)
        connect(m_theme, &Theme::changed, this, &Backend::changed);
    if (m_filePicker) {
        connect(m_filePicker, &FilePicker::openSelected, this, &Backend::openUrl);
        connect(m_filePicker, &FilePicker::exportSelected, this,
                [this](const QUrl &url, const QString &what) { exportTo(url.toLocalFile(), what); });
        connect(m_filePicker, &FilePicker::failed, this, &Backend::fail);
    }
    // Sliders move in small steps; wait for a pause before thresholding again.
    m_detectDelay.setSingleShot(true);
    m_detectDelay.setInterval(150);
    connect(&m_detectDelay, &QTimer::timeout, this, &Backend::detect);
    connect(&m_watcher, &QFutureWatcher<DetectResult>::finished, this, [this] {
        if (!m_watcher.isFinished())
            return;
        // Settings changed, auto-suggest was turned off, or another image was
        // opened while this ran: its blobs are stale.
        if (m_watcher.property("generation").toInt() != m_detectGeneration) {
            detect();
            return;
        }
        const DetectResult result = m_watcher.result();
        m_blobs = result.blobs;
        m_rejected.clear();
        if (m_autoThreshold)
            m_threshold = result.threshold;
        m_foundDark = result.dark;
        refilterSuggestions();
    });
    m_session = m_saved = Session::withDefaults({tr("Live"), tr("Dead"), tr("Colony")});
}

Backend::~Backend() { m_watcher.waitForFinished(); }

QString Backend::fileName() const { return QFileInfo(m_session.imagePath).fileName(); }

QColor Backend::accent() const { return QColor(m_theme ? m_theme->accent() : QStringLiteral("#FFD60A")); }

QVariantList Backend::classes() const {
    QVariantList list;
    const auto counts = m_session.classCounts();
    int total = 0;
    for (int count : counts)
        total += count;
    for (int i = 0; i < m_session.classes.size(); ++i) {
        const double percent = total ? 100.0 * counts[i] / total : -1.0;
        list << QVariantMap{{"name", m_session.classes[i].name},
                            {"shape", m_session.classes[i].shape},
                            {"color", classColor(accent(), i).name()},
                            {"count", counts[i]},
                            {"percent", percent}};
    }
    return list;
}

void Backend::setCurrentClass(int index) {
    if (index < 0 || index >= m_session.classes.size() || index == m_currentClass)
        return;
    m_currentClass = index;
    emit changed();
    emit suggestionsChanged();
}

double Backend::viability() const {
    const double value = m_session.viability();
    return std::isnan(value) ? -1 : value;
}

QString Backend::liveName() const {
    return m_session.liveClass >= 0 && m_session.liveClass < m_session.classes.size()
               ? m_session.classes[m_session.liveClass].name : QString();
}

QString Backend::deadName() const {
    return m_session.deadClass >= 0 && m_session.deadClass < m_session.classes.size()
               ? m_session.classes[m_session.deadClass].name : QString();
}

void Backend::setRoiDrawShape(const QString &shape) {
    if ((shape != "rect" && shape != "circle") || shape == m_roiDrawShape)
        return;
    m_roiDrawShape = shape;
    emit changed();
}

double Backend::areaUm2() const {
    const double value = m_session.areaUm2();
    return std::isnan(value) ? -1 : value;
}

void Backend::setPixelsPerUm(double value) {
    if (value < 0 || !std::isfinite(value) || qFuzzyCompare(value + 1, m_session.pixelsPerUm + 1))
        return;
    remember();
    m_session.pixelsPerUm = value;
    touch();
}

void Backend::setDilution(double value) {
    if (value <= 0 || !std::isfinite(value) || qFuzzyCompare(value, m_session.dilution))
        return;
    remember();
    m_session.dilution = value;
    touch();
}

void Backend::setSquares(int value) {
    if (value < 0 || value == m_session.squares)
        return;
    remember();
    m_session.squares = value;
    touch();
}

double Backend::concentration() const {
    const double value = m_session.concentration(m_session.total());
    return std::isnan(value) ? -1 : value;
}

void Backend::open() {
    if (!m_filePicker)
        return fail(tr("No file chooser is available."));
    m_filePicker->openImage();
}

bool Backend::openPath(const QString &path) {
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    if (!info.exists()) {
        fail(tr("%1 doesn't exist.").arg(path));
        return false;
    }

    Session session;
    QString sessionPath;
    if (info.suffix().toLower() == "cellcount") {
        QList<Problem> problems;
        if (!Session::load(info.absoluteFilePath(), &session, &problems)) {
            QStringList errors;
            for (const auto &problem : problems)
                if (problem.error)
                    errors << problem.message;
            fail(tr("Could not open %1: %2").arg(info.fileName(), errors.join("; ")));
            return false;
        }
        sessionPath = info.absoluteFilePath();
    } else {
        // A session saved beside the image picks up where it left off. One
        // that can't be read is reported and left alone, rather than replaced
        // by a fresh session that the next save would write over it.
        const QString imagePath = info.absoluteFilePath();
        sessionPath = Session::sessionPathFor(imagePath);
        if (QFileInfo::exists(sessionPath)) {
            QList<Problem> problems;
            if (!Session::load(sessionPath, &session, &problems)) {
                QStringList errors;
                for (const auto &problem : problems)
                    if (problem.error)
                        errors << problem.message;
                fail(tr("%1 beside this image can't be read, so it was left as it is: %2. Fix it, or move it "
                        "aside to start counting afresh.")
                         .arg(QFileInfo(sessionPath).fileName(), errors.join("; ")));
                return false;
            }
            if (QFileInfo(session.imagePath) != info) {
                fail(tr("%1 belongs to %2, not this image, so it was left as it is. Move it aside to count "
                        "this image.")
                         .arg(QFileInfo(sessionPath).fileName(), session.imagePath));
                return false;
            }
        } else {
            session = Session::withDefaults({tr("Live"), tr("Dead"), tr("Colony")});
            session.imagePath = imagePath;
        }
    }

    QString error;
    const QImage image = loadImage(session.imagePath, &error);
    if (image.isNull()) {
        fail(error);
        return false;
    }

    m_watcher.waitForFinished();
    ++m_detectGeneration;
    m_image = image;
    session.imageSize = image.size();
    m_session = session;
    m_saved = session;
    m_sessionPath = sessionPath;
    m_undo.clear();
    m_redo.clear();
    m_currentClass = 0;
    m_blobs.clear();
    m_rejected.clear();
    m_suggestions.clear();
    m_error.clear();
    m_status.clear();
    emit imageChanged();
    emit changed();
    emit messageChanged();
    if (m_autoSuggest)
        detect();
    else
        emit suggestionsChanged();
    return true;
}

bool Backend::save() {
    if (!hasImage())
        return false;
    QString error;
    if (!m_session.save(m_sessionPath, &error)) {
        fail(tr("Could not save %1: %2").arg(QFileInfo(m_sessionPath).fileName(), error));
        return false;
    }
    m_saved = m_session;
    report(tr("Saved %1").arg(QFileInfo(m_sessionPath).fileName()));
    emit changed();
    return true;
}

void Backend::exportCounts() {
    if (!hasImage())
        return;
    if (!m_filePicker)
        return fail(tr("No file chooser is available."));
    const QFileInfo image(m_session.imagePath);
    m_filePicker->exportCounts(QUrl::fromLocalFile(image.absoluteDir().filePath(image.completeBaseName() + "-counts.csv")));
}

bool Backend::exportTo(const QString &path, const QString &what) {
    if (!hasImage() || path.isEmpty())
        return false;
    const QFileInfo target(path);
    const QString base = target.absoluteDir().filePath(target.completeBaseName());
    // The file chooser suggests one name for every kind, so the extension
    // follows the kind picked: a PNG never lands in a .csv file.
    auto named = [&](const QString &suffix) {
        return target.suffix().toLower() == suffix ? target.absoluteFilePath() : base + "." + suffix;
    };
    QList<QPair<QString, QString>> outputs; // kind, path
    if (what == "all")
        outputs = {{"markers", base + "-markers.csv"}, {"summary", base + "-summary.csv"}, {"png", base + ".png"}};
    else if (what == "markers" || what == "summary")
        outputs = {{what, named("csv")}};
    else if (what == "png")
        outputs = {{what, named("png")}};
    else {
        fail(tr("Unknown export \"%1\".").arg(what));
        return false;
    }

    // The original image is never written to, even when asked to, and the
    // check comes first so a refused export leaves nothing half written.
    for (const auto &output : outputs)
        if (QFileInfo(output.second) == QFileInfo(m_session.imagePath)) {
            fail(tr("Exporting would overwrite the original image, so nothing was written."));
            return false;
        }

    QStringList written;
    for (const auto &[kind, output] : outputs) {
        QString error;
        bool ok;
        if (kind == "png") {
            QSaveFile file(output);
            ok = file.open(QIODevice::WriteOnly) && renderAnnotated(m_image, m_session, accent()).save(&file, "PNG") &&
                 file.commit();
            error = file.errorString();
        } else {
            const QString csv = kind == "markers" ? m_session.markersCsv() : m_session.summaryCsv();
            ok = writeAtomically(output, csv.toUtf8(), &error);
        }
        if (!ok) {
            fail(tr("Could not write %1: %2").arg(QFileInfo(output).fileName(), error));
            return false;
        }
        written << QFileInfo(output).fileName();
    }
    report(tr("Exported %1").arg(written.join(", ")));
    return true;
}

void Backend::addMarker(double x, double y) {
    if (!hasImage() || x < 0 || y < 0 || x > m_image.width() || y > m_image.height())
        return;
    remember();
    m_session.markers << Marker{x, y, m_currentClass};
    touch();
}

int Backend::markerAt(double x, double y, double radius) const {
    int best = -1;
    double bestDistance = radius * radius;
    for (int i = 0; i < m_session.markers.size(); ++i) {
        const double d = distanceSquared(x, y, m_session.markers[i].x, m_session.markers[i].y);
        if (d <= bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

void Backend::beginMove(int index) {
    if (index < 0 || index >= m_session.markers.size())
        return;
    m_moveStart = m_session;
    m_moving = true;
}

void Backend::moveMarker(int index, double x, double y) {
    if (!m_moving || index < 0 || index >= m_session.markers.size())
        return;
    m_session.markers[index].x = std::clamp(x, 0.0, double(m_image.width()));
    m_session.markers[index].y = std::clamp(y, 0.0, double(m_image.height()));
    refilterSuggestions();
    emit changed();
}

void Backend::endMove() {
    if (!m_moving)
        return;
    m_moving = false;
    // One undo step for the whole drag, and none for a click that didn't move.
    if (m_moveStart == m_session)
        return;
    m_undo << m_moveStart;
    m_redo.clear();
    touch();
}

bool Backend::removeNearest(double x, double y, double radius) {
    const int index = markerAt(x, y, radius);
    if (index < 0)
        return false;
    remember();
    m_session.markers.removeAt(index);
    touch();
    return true;
}

void Backend::setRoi(const QString &shape, double x, double y, double width, double height) {
    if (!hasImage() || (shape != "rect" && shape != "circle"))
        return;
    QRectF rect = QRectF(x, y, width, height).normalized();
    if (shape == "circle") {
        // Drawn from the center outward, so the drag length is the radius.
        const double radius = std::hypot(width, height);
        rect = QRectF(x - radius, y - radius, radius * 2, radius * 2);
    }
    if (shape == "rect")
        rect = rect.intersected(QRectF(QPointF(0, 0), QSizeF(m_image.size())));
    if (rect.width() < 2 || rect.height() < 2)
        return;
    remember();
    m_session.roi = Roi{shape, rect};
    m_roiDrawShape = shape;
    touch();
}

void Backend::clearRoi() {
    if (!m_session.roi.isSet())
        return;
    remember();
    m_session.roi = Roi{};
    touch();
}

QList<QPointF> Backend::suggestions() const {
    QList<QPointF> points;
    for (int index : m_suggestions)
        points << QPointF(m_blobs[index].x, m_blobs[index].y);
    return points;
}

int Backend::suggestionAt(double x, double y, double radius) const {
    int best = -1;
    double bestDistance = radius * radius;
    for (int i = 0; i < m_suggestions.size(); ++i) {
        const Blob &blob = m_blobs[m_suggestions[i]];
        const double d = distanceSquared(x, y, blob.x, blob.y);
        if (d <= bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

void Backend::acceptSuggestion(int index) {
    if (index < 0 || index >= m_suggestions.size())
        return;
    const Blob &blob = m_blobs[m_suggestions[index]];
    remember();
    m_session.markers << Marker{blob.x, blob.y, m_currentClass};
    touch();
}

void Backend::rejectSuggestion(int index) {
    if (index < 0 || index >= m_suggestions.size())
        return;
    m_rejected.insert(m_suggestions[index]);
    refilterSuggestions();
}

void Backend::acceptAllSuggestions() {
    if (m_suggestions.isEmpty())
        return;
    remember();
    for (int index : m_suggestions)
        m_session.markers << Marker{m_blobs[index].x, m_blobs[index].y, m_currentClass};
    report(tr("Accepted %n suggestions", "", m_suggestions.size()));
    touch();
}

void Backend::addClass() {
    if (m_session.classes.size() >= Session::maxClasses)
        return;
    remember();
    QString shape;
    for (const auto &candidate : Session::shapes()) {
        bool used = false;
        for (const auto &cls : m_session.classes)
            used = used || cls.shape == candidate;
        if (!used) {
            shape = candidate;
            break;
        }
    }
    m_session.classes << CountClass{tr("Class %1").arg(m_session.classes.size() + 1), shape};
    m_currentClass = m_session.classes.size() - 1;
    touch();
}

void Backend::renameClass(int index, const QString &name) {
    const QString trimmed = name.trimmed();
    if (index < 0 || index >= m_session.classes.size() || trimmed.isEmpty() || trimmed == m_session.classes[index].name)
        return;
    remember();
    m_session.classes[index].name = trimmed;
    touch();
}

void Backend::removeClass(int index) {
    if (index < 0 || index >= m_session.classes.size() || m_session.classes.size() <= 1)
        return;
    remember();
    m_session.classes.removeAt(index);
    QList<Marker> kept;
    for (Marker marker : m_session.markers) {
        if (marker.cls == index)
            continue;
        if (marker.cls > index)
            --marker.cls;
        kept << marker;
    }
    m_session.markers = kept;
    auto shift = [index](int &cls) { cls = cls == index ? -1 : cls > index ? cls - 1 : cls; };
    shift(m_session.liveClass);
    shift(m_session.deadClass);
    if (m_session.liveClass < 0 || m_session.deadClass < 0)
        m_session.liveClass = m_session.deadClass = -1;
    if (m_currentClass > index)
        --m_currentClass;
    m_currentClass = std::min(m_currentClass, int(m_session.classes.size()) - 1);
    touch();
}

void Backend::undo() {
    endMove();
    if (m_undo.isEmpty())
        return;
    m_redo << m_session;
    m_session = m_undo.takeLast();
    m_currentClass = std::min(m_currentClass, int(m_session.classes.size()) - 1);
    touch();
}

void Backend::redo() {
    endMove();
    if (m_redo.isEmpty())
        return;
    m_undo << m_session;
    m_session = m_redo.takeLast();
    m_currentClass = std::min(m_currentClass, int(m_session.classes.size()) - 1);
    touch();
}

void Backend::clearError() {
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit messageChanged();
}

void Backend::remember() {
    endMove();
    m_undo << m_session;
    if (m_undo.size() > undoLimit)
        m_undo.removeFirst();
    m_redo.clear();
}

void Backend::touch() {
    refilterSuggestions();
    emit changed();
}

void Backend::fail(const QString &message) {
    m_error = message;
    emit messageChanged();
}

void Backend::report(const QString &message) {
    m_status = message;
    emit messageChanged();
    emit statusReported(message);
}

void Backend::setAutoSuggest(bool on) {
    if (on == m_autoSuggest)
        return;
    m_autoSuggest = on;
    if (on && hasImage()) {
        detect();
        return;
    }
    ++m_detectGeneration;
    m_blobs.clear();
    m_rejected.clear();
    m_suggestions.clear();
    emit suggestionsChanged();
}

void Backend::setAutoThreshold(bool on) {
    if (on == m_autoThreshold)
        return;
    m_autoThreshold = on;
    emit suggestionsChanged();
    if (m_autoSuggest)
        m_detectDelay.start();
}

void Backend::setThreshold(int value) {
    value = std::clamp(value, 0, 255);
    if (value == m_threshold && !m_autoThreshold)
        return;
    m_threshold = value;
    m_autoThreshold = false;
    emit suggestionsChanged();
    if (m_autoSuggest)
        m_detectDelay.start();
}

void Backend::setObjects(const QString &objects) {
    if ((objects != "auto" && objects != "dark" && objects != "light") || objects == m_objects)
        return;
    m_objects = objects;
    emit suggestionsChanged();
    if (m_autoSuggest)
        detect();
}

void Backend::setMinArea(int value) {
    value = std::max(1, value);
    if (value == m_minArea)
        return;
    m_minArea = value;
    emit suggestionsChanged();
    if (m_autoSuggest)
        m_detectDelay.start();
}

void Backend::detect() {
    if (!hasImage() || !m_autoSuggest)
        return;
    ++m_detectGeneration;
    if (m_watcher.isRunning())
        return; // the finished handler starts again with the latest settings
    DetectOptions options;
    options.threshold = m_autoThreshold ? -1 : m_threshold;
    options.minArea = m_minArea;
    options.polarity = m_objects == "dark" ? DetectOptions::Dark
                     : m_objects == "light" ? DetectOptions::Light
                                            : DetectOptions::Auto;
    // Anything bigger than a twentieth of the image is a smear or the
    // background, not one cell or colony.
    options.maxArea = int(qint64(m_image.width()) * m_image.height() / 20);
    m_watcher.setProperty("generation", m_detectGeneration);
    m_watcher.setFuture(QtConcurrent::run([image = m_image, options] { return detectBlobs(image, options); }));
    emit suggestionsChanged();
}

// Suggestions hide once a marker sits on them, outside the ROI, or when rejected.
// This runs on every pointer step of a drag, so markers go into a grid as wide
// as the largest blob's radius, and each blob looks only at its neighbouring cells.
void Backend::refilterSuggestions() {
    QList<int> visible;
    if (!m_blobs.isEmpty()) {
        double cell = 8;
        for (const Blob &blob : m_blobs)
            cell = std::max(cell, std::sqrt(blob.area / M_PI));
        auto key = [cell](double x, double y) {
            return (qint64(std::floor(x / cell)) << 32) ^ quint32(qint32(std::floor(y / cell)));
        };
        QHash<qint64, QList<int>> grid;
        for (int i = 0; i < m_session.markers.size(); ++i)
            grid[key(m_session.markers[i].x, m_session.markers[i].y)] << i;

        for (int i = 0; i < m_blobs.size(); ++i) {
            const Blob &blob = m_blobs[i];
            if (m_rejected.contains(i) || !m_session.roi.contains({blob.x, blob.y}))
                continue;
            const double radius = std::max(4.0, std::sqrt(blob.area / M_PI));
            bool covered = false;
            for (int dy = -1; dy <= 1 && !covered; ++dy)
                for (int dx = -1; dx <= 1 && !covered; ++dx)
                    for (int index : grid.value(key(blob.x + dx * cell, blob.y + dy * cell))) {
                        const Marker &marker = m_session.markers[index];
                        if (distanceSquared(marker.x, marker.y, blob.x, blob.y) <= radius * radius) {
                            covered = true;
                            break;
                        }
                    }
            if (!covered)
                visible << i;
        }
    }
    m_suggestions = visible;
    emit suggestionsChanged();
}
