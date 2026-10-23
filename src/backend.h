#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include "detect.h"
#include "session.h"

class FilePicker;
class Theme;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY imageChanged)
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY imageChanged)
    Q_PROPERTY(QVariantList classes READ classes NOTIFY changed)
    Q_PROPERTY(int currentClass READ currentClass WRITE setCurrentClass NOTIFY changed)
    Q_PROPERTY(int maxClasses READ maxClasses CONSTANT)
    Q_PROPERTY(int markerCount READ markerCount NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(double viability READ viability NOTIFY changed)
    Q_PROPERTY(QString liveName READ liveName NOTIFY changed)
    Q_PROPERTY(QString deadName READ deadName NOTIFY changed)
    Q_PROPERTY(QString roiShape READ roiShape NOTIFY changed)
    Q_PROPERTY(QString roiDrawShape READ roiDrawShape WRITE setRoiDrawShape NOTIFY changed)
    Q_PROPERTY(double areaPx READ areaPx NOTIFY changed)
    Q_PROPERTY(double areaUm2 READ areaUm2 NOTIFY changed)
    Q_PROPERTY(double pixelsPerUm READ pixelsPerUm WRITE setPixelsPerUm NOTIFY changed)
    Q_PROPERTY(double dilution READ dilution WRITE setDilution NOTIFY changed)
    Q_PROPERTY(int squares READ squares WRITE setSquares NOTIFY changed)
    Q_PROPERTY(double concentration READ concentration NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(bool autoSuggest READ autoSuggest WRITE setAutoSuggest NOTIFY suggestionsChanged)
    Q_PROPERTY(bool autoThreshold READ autoThreshold WRITE setAutoThreshold NOTIFY suggestionsChanged)
    Q_PROPERTY(int threshold READ threshold WRITE setThreshold NOTIFY suggestionsChanged)
    Q_PROPERTY(int minArea READ minArea WRITE setMinArea NOTIFY suggestionsChanged)
    Q_PROPERTY(int suggestionCount READ suggestionCount NOTIFY suggestionsChanged)
    Q_PROPERTY(bool detecting READ detecting NOTIFY suggestionsChanged)
    Q_PROPERTY(QString error READ error NOTIFY messageChanged)
    Q_PROPERTY(QString status READ status NOTIFY messageChanged)

public:
    explicit Backend(Theme *theme = nullptr, FilePicker *filePicker = nullptr, QObject *parent = nullptr);
    ~Backend() override;

    bool hasImage() const { return !m_image.isNull(); }
    QString fileName() const;
    QSize imageSize() const { return m_image.size(); }
    QVariantList classes() const;
    int currentClass() const { return m_currentClass; }
    void setCurrentClass(int index);
    int maxClasses() const { return Session::maxClasses; }
    int markerCount() const { return m_session.markers.size(); }
    int total() const { return m_session.total(); }
    double viability() const; // -1 when it doesn't apply
    QString liveName() const;
    QString deadName() const;
    QString roiShape() const { return m_session.roi.isSet() ? m_session.roi.shape : QString(); }
    QString roiDrawShape() const { return m_roiDrawShape; }
    void setRoiDrawShape(const QString &shape);
    double areaPx() const { return m_session.areaPx(); }
    double areaUm2() const; // -1 without a scale
    double pixelsPerUm() const { return m_session.pixelsPerUm; }
    void setPixelsPerUm(double value);
    double dilution() const { return m_session.dilution; }
    void setDilution(double value);
    int squares() const { return m_session.squares; }
    void setSquares(int value);
    double concentration() const; // -1 without squares
    bool dirty() const { return hasImage() && !(m_session == m_saved); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool autoSuggest() const { return m_autoSuggest; }
    void setAutoSuggest(bool on);
    bool autoThreshold() const { return m_autoThreshold; }
    void setAutoThreshold(bool on);
    int threshold() const { return m_threshold; }
    void setThreshold(int value);
    int minArea() const { return m_minArea; }
    void setMinArea(int value);
    int suggestionCount() const { return m_suggestions.size(); }
    bool detecting() const { return m_watcher.isRunning(); }
    QString error() const { return m_error; }
    QString status() const { return m_status; }

    const QImage &image() const { return m_image; }
    const Session &session() const { return m_session; }
    QString sessionPath() const { return m_sessionPath; }
    QList<QPointF> suggestions() const;
    QColor accent() const;

    Q_INVOKABLE void open();
    Q_INVOKABLE bool openPath(const QString &path);
    Q_INVOKABLE bool openUrl(const QUrl &url) { return openPath(url.toLocalFile()); }
    Q_INVOKABLE bool save();
    Q_INVOKABLE void exportCounts();
    Q_INVOKABLE bool exportTo(const QString &path, const QString &what);

    Q_INVOKABLE void addMarker(double x, double y);
    Q_INVOKABLE int markerAt(double x, double y, double radius) const;
    Q_INVOKABLE void beginMove(int index);
    Q_INVOKABLE void moveMarker(int index, double x, double y);
    Q_INVOKABLE void endMove();
    Q_INVOKABLE bool removeNearest(double x, double y, double radius);
    // A rectangle is its corner and size; a circle is its center and the drag
    // from it, whose length is the radius.
    Q_INVOKABLE void setRoi(const QString &shape, double x, double y, double width, double height);
    Q_INVOKABLE void clearRoi();

    Q_INVOKABLE int suggestionAt(double x, double y, double radius) const;
    Q_INVOKABLE void acceptSuggestion(int index);
    Q_INVOKABLE void rejectSuggestion(int index);
    Q_INVOKABLE void acceptAllSuggestions();

    Q_INVOKABLE void addClass();
    Q_INVOKABLE void renameClass(int index, const QString &name);
    Q_INVOKABLE void removeClass(int index);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearError();

signals:
    void imageChanged();
    void changed();
    void suggestionsChanged();
    void messageChanged();
    void statusReported(const QString &message);

private:
    void remember();
    void touch();
    void fail(const QString &message);
    void report(const QString &message);
    void detect();
    void refilterSuggestions();

    Theme *m_theme;
    FilePicker *m_filePicker;
    QImage m_image;
    Session m_session;
    Session m_saved;
    QString m_sessionPath;
    QList<Session> m_undo, m_redo;
    Session m_moveStart;
    bool m_moving = false;
    int m_currentClass = 0;
    QString m_roiDrawShape = "rect";

    bool m_autoSuggest = false;
    bool m_autoThreshold = true;
    int m_threshold = 128;
    int m_minArea = 12;
    int m_detectGeneration = 0;
    QFutureWatcher<DetectResult> m_watcher;
    QTimer m_detectDelay;
    QList<Blob> m_blobs;
    QSet<int> m_rejected;
    QList<int> m_suggestions; // indices into m_blobs

    QString m_error;
    QString m_status;
};
