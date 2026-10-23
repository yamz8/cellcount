#include "spacekey.h"

#include <QGuiApplication>
#include <QInputMethodQueryEvent>
#include <QKeyEvent>

namespace {
bool typingText() {
    QObject *focus = QGuiApplication::focusObject();
    if (!focus)
        return false;
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(focus, &query);
    return query.value(Qt::ImEnabled).toBool();
}
} // namespace

bool SpaceKey::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
        return QObject::eventFilter(watched, event);
    auto *key = static_cast<QKeyEvent *>(event);
    // Events reach the filter once per object on their way (window, item), so
    // react only at the window, and only to a bare Space.
    if (key->key() != Qt::Key_Space || key->modifiers() != Qt::NoModifier || !watched->isWindowType())
        return QObject::eventFilter(watched, event);
    if (typingText() && !m_held)
        return false;
    if (key->isAutoRepeat())
        return true;
    if (event->type() == QEvent::KeyPress) {
        m_held = true;
        m_used = false;
        emit heldChanged();
    } else if (m_held) {
        m_held = false;
        emit heldChanged();
        if (!m_used)
            emit tapped();
    }
    return true;
}
