import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A button with the window's rounding: the accent fill for the main action,
// a quiet fill otherwise. Material's Button brings a full rounding and fill
// of its own, so the background is drawn here.
Button {
    id: button
    property bool primary: false

    focusPolicy: Qt.NoFocus
    Material.roundedScale: theme.radius
    Material.foreground: primary ? theme.accentForeground : checked ? theme.accent : "#e8e8ea"

    background: Rectangle {
        implicitWidth: 64
        implicitHeight: 36
        y: (button.height - height) / 2
        height: 36
        radius: theme.radius
        color: button.primary ? (button.down ? Qt.darker(theme.accent, 1.15) : button.hovered ? Qt.lighter(theme.accent, 1.08) : theme.accent)
                              : Qt.rgba(1, 1, 1, button.down ? 0.2 : button.checked ? 0.14 : button.hovered ? 0.1 : 0.06)
        border.color: button.checked && !button.primary ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
    }
}
