#pragma once

#include <QObject>
#include <QUrl>

// Open and save dialogs, behind an interface so tests can answer them.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openImage() = 0;
    virtual void exportCounts(const QUrl &suggestedUrl) = 0;

signals:
    void openSelected(const QUrl &url);
    // what is "all", "markers", "summary" or "png".
    void exportSelected(const QUrl &url, const QString &what);
    void failed(const QString &message);
};
