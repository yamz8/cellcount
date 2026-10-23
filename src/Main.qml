import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Cellcount

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 820
    minimumHeight: 540
    visible: true
    title: backend.hasImage ? (backend.dirty ? "● " : "") + backend.fileName + " — Cellcount" : "Cellcount"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool roiMode: false
    property bool closing: false
    property var pendingAction: null

    // Numbers follow the Omarchy language's separators and digits, and
    // re-render when it changes.
    function num(value, decimals) {
        translator.language
        return Number(value).toLocaleString(Qt.locale(), 'f', decimals)
    }

    // Anything that would drop unsaved counting asks first.
    function guard(action) {
        if (backend.dirty) {
            pendingAction = action
            unsavedDialog.open()
        } else {
            action()
        }
    }

    function quit() {
        guard(function() { win.closing = true; Qt.quit() })
    }

    onClosing: (close) => {
        if (backend.dirty && !win.closing) {
            close.accepted = false
            quit()
        }
    }

    Shortcut { sequences: ["?", "Shift+?"]; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: win.quit() }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: win.guard(backend.open) }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; onActivated: backend.save() }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; onActivated: backend.exportCounts() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", StandardKey.Redo]; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut { sequence: "A"; context: Qt.ApplicationShortcut; onActivated: backend.acceptAllSuggestions() }
    Shortcut {
        sequence: "R"
        context: Qt.ApplicationShortcut
        onActivated: win.roiMode = backend.hasImage && !win.roiMode
    }
    Shortcut { sequence: "Shift+R"; context: Qt.ApplicationShortcut; onActivated: backend.clearRoi() }
    Shortcut { sequences: ["+", "="]; context: Qt.ApplicationShortcut; onActivated: canvas.zoomBy(1.25) }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; onActivated: canvas.zoomBy(0.8) }
    Shortcut { sequence: "0"; context: Qt.ApplicationShortcut; onActivated: canvas.fit() }
    Shortcut {
        sequences: ["Backspace", "Delete"]
        context: Qt.ApplicationShortcut
        onActivated: {
            if (!pointer.containsMouse)
                return
            const p = canvas.toImage(pointer.mouseX, pointer.mouseY)
            backend.removeNearest(p.x, p.y, pointer.hit * 2)
            canvas.highlighted = -1
        }
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        onActivated: {
            win.helpVisible = false
            win.roiMode = false
            canvas.previewShape = ""
        }
    }
    Instantiator {
        model: 9
        delegate: Shortcut {
            required property int index
            sequence: String(index + 1)
            context: Qt.ApplicationShortcut
            onActivated: backend.currentClass = index
        }
    }

    Connections {
        target: spaceKey
        function onTapped() {
            if (backend.hasImage)
                backend.autoSuggest = !backend.autoSuggest
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                id: stage
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                CountCanvas {
                    id: canvas
                    anchors.fill: parent
                    source: backend
                    background: "#141417"
                    // The image itself never mirrors.
                    LayoutMirroring.enabled: false
                }

                MouseArea {
                    id: pointer
                    anchors.fill: parent
                    enabled: backend.hasImage
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    hoverEnabled: true
                    preventStealing: true
                    LayoutMirroring.enabled: false

                    property bool panning: false
                    property bool drawingRoi: false
                    property int dragIndex: -1
                    property real lastX: 0
                    property real lastY: 0
                    property point roiStart
                    // How close, in image pixels, counts as on a marker.
                    readonly property real hit: (canvas.markerRadius + 5) / canvas.zoom

                    cursorShape: panning ? Qt.ClosedHandCursor
                               : spaceKey.held ? Qt.OpenHandCursor
                               : dragIndex >= 0 ? Qt.SizeAllCursor
                               : canvas.highlighted >= 0 && !win.roiMode ? Qt.PointingHandCursor
                               : Qt.CrossCursor

                    onPressed: (mouse) => {
                        lastX = mouse.x
                        lastY = mouse.y
                        if (mouse.button === Qt.MiddleButton || (mouse.button === Qt.LeftButton && spaceKey.held)) {
                            panning = true
                            if (spaceKey.held)
                                spaceKey.markUsed()
                            return
                        }
                        const p = canvas.toImage(mouse.x, mouse.y)
                        if (mouse.button === Qt.RightButton) {
                            if (backend.removeNearest(p.x, p.y, hit * 2)) {
                                canvas.highlighted = -1
                                return
                            }
                            const rejected = backend.suggestionAt(p.x, p.y, hit * 1.4)
                            if (rejected >= 0)
                                backend.rejectSuggestion(rejected)
                            return
                        }
                        if (win.roiMode) {
                            drawingRoi = true
                            roiStart = p
                            canvas.previewShape = backend.roiDrawShape
                            canvas.previewRect = Qt.rect(p.x, p.y, 0, 0)
                            return
                        }
                        const index = backend.markerAt(p.x, p.y, hit)
                        if (index >= 0) {
                            dragIndex = index
                            backend.beginMove(index)
                            return
                        }
                        const suggestion = backend.suggestionAt(p.x, p.y, hit * 1.4)
                        if (suggestion >= 0) {
                            backend.acceptSuggestion(suggestion)
                            return
                        }
                        if (canvas.insideImage(mouse.x, mouse.y))
                            backend.addMarker(p.x, p.y)
                    }
                    onPositionChanged: (mouse) => {
                        const p = canvas.toImage(mouse.x, mouse.y)
                        if (panning) {
                            canvas.panBy(mouse.x - lastX, mouse.y - lastY)
                        } else if (drawingRoi) {
                            canvas.previewRect = Qt.rect(roiStart.x, roiStart.y, p.x - roiStart.x, p.y - roiStart.y)
                        } else if (dragIndex >= 0) {
                            backend.moveMarker(dragIndex, p.x, p.y)
                        } else {
                            canvas.highlighted = win.roiMode ? -1 : backend.markerAt(p.x, p.y, hit)
                        }
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                    onReleased: {
                        if (drawingRoi) {
                            const r = canvas.previewRect
                            backend.setRoi(canvas.previewShape, r.x, r.y, r.width, r.height)
                            canvas.previewShape = ""
                            drawingRoi = false
                            win.roiMode = false
                        }
                        if (dragIndex >= 0) {
                            backend.endMove()
                            dragIndex = -1
                        }
                        panning = false
                    }
                    onExited: canvas.highlighted = -1
                    onWheel: (wheel) => canvas.zoomAt(wheel.x, wheel.y, Math.pow(1.0015, wheel.angleDelta.y))
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: !backend.hasImage
                    spacing: 16

                    MarkerIcon {
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 56
                        implicitHeight: 56
                        shape: "circle"
                        color: theme.accent
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Count cells and colonies by clicking")
                        font.pixelSize: 22
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Open a PNG, JPEG or TIFF image, or drop one here.")
                        opacity: 0.6
                    }
                    ActionButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Open image…")
                        primary: true
                        onClicked: backend.open()
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Ctrl+O to open · ? for every shortcut")
                        opacity: 0.45
                        font.pixelSize: 12
                    }
                }

                // What the pointer does right now, when it isn't just placing markers.
                Rectangle {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.topMargin: 14
                    visible: hint.text !== ""
                    width: hint.implicitWidth + 28
                    height: hint.implicitHeight + 14
                    radius: theme.radius
                    color: "#e6141417"
                    border.color: Qt.rgba(1, 1, 1, 0.08)

                    Label {
                        id: hint
                        anchors.centerIn: parent
                        font.pixelSize: 13
                        text: win.roiMode
                              ? (backend.roiDrawShape === "circle"
                                 ? qsTr("Drag from the center to draw a circular region · Esc cancels")
                                 : qsTr("Drag to draw a rectangular region · Esc cancels"))
                              : backend.autoSuggest
                                ? (backend.detecting ? qsTr("Looking for cells…")
                                   : qsTr("%n suggestions: click to accept, right-click to reject, A accepts all", "", backend.suggestionCount))
                                : ""
                    }
                }

                Rectangle {
                    id: errorBanner
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 14
                    anchors.topMargin: hint.parent.visible ? 58 : 14
                    visible: backend.error !== ""
                    height: errorRow.implicitHeight + 16
                    radius: theme.radius
                    color: "#5c1f24"
                    border.color: "#a8424a"

                    RowLayout {
                        id: errorRow
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 6
                        Label {
                            Layout.fillWidth: true
                            text: backend.error
                            wrapMode: Text.Wrap
                        }
                        ToolButton {
                            text: "✕"
                            focusPolicy: Qt.NoFocus
                            onClicked: backend.clearError()
                        }
                    }
                }

                Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    visible: backend.hasImage
                    opacity: 0.55
                    font.pixelSize: 12
                    text: qsTr("%1 × %2 px · %3% · ? shortcuts")
                          .arg(backend.imageSize.width).arg(backend.imageSize.height)
                          .arg(win.num(canvas.zoom * 100, 0))
                }

                Rectangle {
                    id: toast
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottomMargin: 16
                    width: toastLabel.implicitWidth + 28
                    height: toastLabel.implicitHeight + 14
                    radius: theme.radius
                    color: "#e6141417"
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                    opacity: 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }

                    Label { id: toastLabel; anchors.centerIn: parent; text: backend.status }
                    Timer { id: toastTimer; interval: 2600; onTriggered: toast.opacity = 0 }
                    Connections {
                        target: backend
                        function onMessageChanged() {
                            if (backend.status !== "") {
                                toast.opacity = 1
                                toastTimer.restart()
                            }
                        }
                    }
                }

                DropArea {
                    anchors.fill: parent
                    onDropped: (drop) => {
                        if (drop.hasUrls) {
                            const url = drop.urls[0]
                            win.guard(function() { backend.openUrl(url) })
                        }
                    }
                }
            }

            SidePanel {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                onDrawRoi: win.roiMode = !win.roiMode
                roiMode: win.roiMode
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onClose: win.helpVisible = false
        }
    }

    Dialog {
        id: unsavedDialog
        anchors.centerIn: parent
        modal: true
        width: Math.min(460, win.width - 48)
        title: qsTr("Save your counts?")
        Material.theme: Material.Dark
        Material.background: "#18181b"
        background: Rectangle {
            color: "#18181b"
            radius: theme.radius
            border.color: Qt.rgba(1, 1, 1, 0.1)
        }
        Overlay.modal: Rectangle { color: "#99000000" }
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Material.roundedScale: theme.radius

        Label {
            width: unsavedDialog.availableWidth
            text: qsTr("%1 has changes that aren't saved.").arg(backend.fileName)
            wrapMode: Text.Wrap
        }
        onAccepted: {
            if (backend.save() && win.pendingAction)
                win.pendingAction()
        }
        onDiscarded: {
            close()
            if (win.pendingAction)
                win.pendingAction()
        }
    }
}
