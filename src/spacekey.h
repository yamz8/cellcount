#pragma once

#include <QObject>

// Space does two things: a tap toggles auto-suggest, and held down it turns a
// drag into a pan. A Shortcut can't tell the two apart, so this watches the key
// for the whole application, and leaves it alone while a text field has focus.
class SpaceKey : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool held READ held NOTIFY heldChanged)

public:
    explicit SpaceKey(QObject *parent = nullptr) : QObject(parent) {}

    bool held() const { return m_held; }
    // Called when a drag used the key, so letting go doesn't count as a tap.
    Q_INVOKABLE void markUsed() { m_used = true; }

signals:
    void heldChanged();
    void tapped();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool m_held = false;
    bool m_used = false;
};
