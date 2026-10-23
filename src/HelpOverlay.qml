import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Rectangle {
    id: overlay
    color: "#cc0e0e10"
    Material.roundedScale: theme.radius

    signal close()

    readonly property var shortcuts: [
        [qsTr("Click"), qsTr("Place a marker of the current class")],
        [qsTr("Drag"), qsTr("Move a marker")],
        [qsTr("Right-click") + " / Backspace", qsTr("Remove the nearest marker")],
        ["1 – 9", qsTr("Pick the class")],
        ["Space", qsTr("Turn auto-suggest on or off")],
        ["A", qsTr("Accept all suggestions")],
        ["R", qsTr("Draw the region of interest")],
        ["Shift+R", qsTr("Clear the region of interest")],
        [qsTr("Wheel") + "  + / −", qsTr("Zoom in and out")],
        ["0", qsTr("Fit the image")],
        [qsTr("Middle-drag") + " / Space+" + qsTr("drag"), qsTr("Pan")],
        ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo and redo")],
        ["Ctrl+O", qsTr("Open an image or session")],
        ["Ctrl+S", qsTr("Save the session beside the image")],
        ["Ctrl+E", qsTr("Export CSV and annotated PNG")],
        ["?", qsTr("Show or hide these shortcuts")],
        ["Q", qsTr("Quit")]
    ]

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: overlay.close()
        onWheel: {}
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, grid.implicitWidth + 64)
        height: column.implicitHeight + 48
        radius: theme.radius
        color: "#18181b"
        border.color: Qt.rgba(1, 1, 1, 0.08)

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            Label {
                text: qsTr("Keyboard shortcuts")
                font.pixelSize: 18
                font.bold: true
            }

            GridLayout {
                id: grid
                columns: 2
                columnSpacing: 28
                rowSpacing: 8

                Repeater {
                    model: overlay.shortcuts
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        text: modelData[0]
                        font.family: "monospace"
                        color: theme.accent
                    }
                }
                Repeater {
                    model: overlay.shortcuts
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 1
                        text: modelData[1]
                    }
                }
            }
        }
    }
}
