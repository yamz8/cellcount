import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Cellcount

Rectangle {
    id: panel
    color: "#0e0e10"
    Material.roundedScale: theme.radius

    property bool roiMode: false
    signal drawRoi()

    function num(value, decimals) {
        translator.language
        return Number(value).toLocaleString(Qt.locale(), 'f', decimals)
    }

    function parse(text) {
        try {
            return Number.fromLocaleString(Qt.locale(), text.trim())
        } catch (e) {
            const value = Number(text.trim())
            return isNaN(value) ? -1 : value
        }
    }

    function areaText() {
        if (!backend.hasImage)
            return "–"
        const px = qsTr("%1 px²").arg(num(backend.areaPx, 0))
        if (backend.areaUm2 < 0)
            return px
        if (backend.areaUm2 >= 1e6)
            return qsTr("%1 mm²").arg(num(backend.areaUm2 / 1e6, 3)) + "  ·  " + px
        return qsTr("%1 µm²").arg(num(backend.areaUm2, 0)) + "  ·  " + px
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Qt.rgba(1, 1, 1, 0.07)
    }

    component SectionLabel: Label {
        font.pixelSize: 11
        font.letterSpacing: 1.2
        font.capitalization: Font.AllUppercase
        opacity: 0.5
        Layout.topMargin: 14
    }

    component NumberField: TextField {
        Layout.fillWidth: true
        Material.roundedScale: theme.radius
        selectByMouse: true
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        Keys.onEscapePressed: focus = false
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.leftMargin: 1
        contentWidth: availableWidth
        enabled: backend.hasImage
        opacity: backend.hasImage ? 1 : 0.4

        ColumnLayout {
            width: scroll.availableWidth - 36
            x: 18
            spacing: 6

            Label {
                Layout.topMargin: 18
                Layout.fillWidth: true
                text: backend.hasImage ? backend.fileName : qsTr("No image open")
                font.pixelSize: 16
                font.bold: true
                elide: Text.ElideMiddle
            }
            Label {
                Layout.fillWidth: true
                visible: backend.hasImage
                text: backend.dirty ? qsTr("Unsaved changes") : qsTr("Saved")
                color: backend.dirty ? theme.accent : Material.foreground
                opacity: backend.dirty ? 1 : 0.5
                font.pixelSize: 12
            }

            SectionLabel { text: qsTr("Classes") }

            // The model is the number of classes, so rows are only rebuilt when
            // one is added or removed, not on every count change.
            Repeater {
                model: backend.classes.length
                delegate: Rectangle {
                    id: row
                    required property int index
                    readonly property var modelData: backend.classes[index] || ({})
                    readonly property bool current: index === backend.currentClass
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: Math.min(4, theme.radius)
                    color: current ? Qt.rgba(1, 1, 1, 0.08) : hover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                    border.color: current ? modelData.color : "transparent"

                    HoverHandler { id: hover }
                    TapHandler {
                        onTapped: backend.currentClass = row.index
                        onDoubleTapped: { nameField.text = row.modelData.name; nameField.visible = true; nameField.forceActiveFocus(); nameField.selectAll() }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 8

                        Label {
                            text: row.index + 1
                            opacity: 0.45
                            font.family: "monospace"
                            font.pixelSize: 12
                        }
                        MarkerIcon {
                            implicitWidth: 20
                            implicitHeight: 20
                            shape: row.modelData.shape
                            color: row.modelData.color
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !nameField.visible
                            text: row.modelData.name
                            elide: Text.ElideRight
                        }
                        TextField {
                            id: nameField
                            Material.roundedScale: theme.radius
                            Layout.fillWidth: true
                            visible: false
                            topPadding: 2
                            bottomPadding: 2
                            onEditingFinished: { backend.renameClass(row.index, text); visible = false }
                            Keys.onEscapePressed: visible = false
                        }
                        Label {
                            text: panel.num(row.modelData.count, 0)
                            font.bold: true
                            font.pixelSize: 15
                        }
                        Label {
                            Layout.preferredWidth: 52
                            horizontalAlignment: Text.AlignRight
                            text: row.modelData.percent < 0 ? "–" : qsTr("%1%").arg(panel.num(row.modelData.percent, 1))
                            opacity: 0.55
                            font.pixelSize: 12
                        }
                        ToolButton {
                            implicitWidth: 26
                            implicitHeight: 26
                            padding: 0
                            text: "✕"
                            font.pixelSize: 11
                            visible: hover.hovered && backend.classes.length > 1
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Remove this class and its markers (Ctrl+Z restores them)")
                            onClicked: backend.removeClass(row.index)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    text: qsTr("Add class")
                    enabled: backend.classes.length < backend.maxClasses
                    onClicked: backend.addClass()
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("Double-click to rename")
                    opacity: 0.4
                    font.pixelSize: 11
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                columns: 2
                rowSpacing: 4
                Label { text: qsTr("Total"); font.pixelSize: 15 }
                Label {
                    Layout.alignment: Qt.AlignRight
                    text: panel.num(backend.total, 0)
                    font.pixelSize: 22
                    font.bold: true
                    color: theme.accent
                }
                Label {
                    visible: backend.liveName !== ""
                    text: qsTr("Viability")
                    ToolTip.visible: viabilityHover.hovered
                    ToolTip.text: qsTr("%1 / (%1 + %2)").arg(backend.liveName).arg(backend.deadName)
                    HoverHandler { id: viabilityHover }
                }
                Label {
                    Layout.alignment: Qt.AlignRight
                    visible: backend.liveName !== ""
                    text: backend.viability < 0 ? "–" : qsTr("%1%").arg(panel.num(backend.viability, 1))
                    font.bold: true
                }
                Label {
                    Layout.columnSpan: 2
                    visible: backend.roiShape !== "" && backend.markerCount > backend.total
                    text: qsTr("%n markers outside the region aren't counted", "", backend.markerCount - backend.total)
                    opacity: 0.5
                    font.pixelSize: 11
                }
            }

            SectionLabel { text: qsTr("Region of interest") }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                ButtonGroup { id: roiShapes }
                ActionButton {
                    text: qsTr("Rectangle")
                    checkable: true
                    checked: backend.roiDrawShape === "rect"
                    ButtonGroup.group: roiShapes
                    onClicked: backend.roiDrawShape = "rect"
                }
                ActionButton {
                    text: qsTr("Circle")
                    checkable: true
                    checked: backend.roiDrawShape === "circle"
                    ButtonGroup.group: roiShapes
                    onClicked: backend.roiDrawShape = "circle"
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    text: panel.roiMode ? qsTr("Drawing… (R)") : qsTr("Draw (R)")
                    primary: panel.roiMode
                    onClicked: panel.drawRoi()
                }
                ActionButton {
                    text: qsTr("Clear (Shift+R)")
                    enabled: backend.roiShape !== ""
                    onClicked: backend.clearRoi()
                }
            }
            Label {
                Layout.fillWidth: true
                text: (backend.roiShape !== "" ? qsTr("Region area") : qsTr("Image area")) + ": " + panel.areaText()
                wrapMode: Text.Wrap
                font.pixelSize: 12
                opacity: 0.75
            }

            SectionLabel { text: qsTr("Scale") }
            NumberField {
                placeholderText: qsTr("Pixels per µm (empty for none)")
                text: backend.pixelsPerUm > 0 ? panel.num(backend.pixelsPerUm, 4).replace(/0+$/, "").replace(/[.,]$/, "") : ""
                onEditingFinished: {
                    const value = text.trim() === "" ? 0 : panel.parse(text)
                    if (value >= 0)
                        backend.pixelsPerUm = value
                }
            }

            SectionLabel { text: qsTr("Hemocytometer") }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                NumberField {
                    placeholderText: qsTr("Dilution factor")
                    text: panel.num(backend.dilution, 2).replace(/0+$/, "").replace(/[.,]$/, "")
                    onEditingFinished: {
                        const value = panel.parse(text)
                        if (value > 0)
                            backend.dilution = value
                    }
                }
                NumberField {
                    placeholderText: qsTr("Squares counted")
                    inputMethodHints: Qt.ImhDigitsOnly
                    text: backend.squares > 0 ? String(backend.squares) : ""
                    onEditingFinished: {
                        const value = text.trim() === "" ? 0 : Math.round(panel.parse(text))
                        if (value >= 0)
                            backend.squares = value
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Cells/mL") }
                Item { Layout.fillWidth: true }
                Label {
                    text: backend.concentration < 0 ? "–" : panel.num(backend.concentration, 0)
                    font.bold: true
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Total ÷ squares × dilution × 10,000 (a large Neubauer square holds 0.1 µL).")
                wrapMode: Text.Wrap
                opacity: 0.45
                font.pixelSize: 11
            }

            SectionLabel { text: qsTr("Auto-suggest") }
            Switch {
                Layout.fillWidth: true
                text: qsTr("Suggest cells (Space)")
                checked: backend.autoSuggest
                focusPolicy: Qt.NoFocus
                onToggled: backend.autoSuggest = checked
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: backend.autoSuggest
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Threshold %1").arg(backend.threshold) }
                    Item { Layout.fillWidth: true }
                    CheckBox {
                        text: qsTr("Automatic")
                        checked: backend.autoThreshold
                        focusPolicy: Qt.NoFocus
                        onToggled: backend.autoThreshold = checked
                    }
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 255
                    stepSize: 1
                    value: backend.threshold
                    focusPolicy: Qt.NoFocus
                    onMoved: backend.threshold = value
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Smallest blob (px)") }
                    Item { Layout.fillWidth: true }
                    SpinBox {
                        Material.roundedScale: theme.radius
                        from: 1
                        to: 100000
                        editable: true
                        value: backend.minArea
                        onValueModified: backend.minArea = value
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: backend.detecting ? qsTr("Looking for cells…")
                                                : qsTr("%n suggestions", "", backend.suggestionCount)
                        opacity: 0.7
                    }
                    ActionButton {
                        text: qsTr("Accept all (A)")
                        enabled: backend.suggestionCount > 0
                        onClicked: backend.acceptAllSuggestions()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 18
                Layout.bottomMargin: 18
                ActionButton {
                    Layout.fillWidth: true
                    text: qsTr("Save")
                    primary: backend.dirty
                    onClicked: backend.save()
                }
                ActionButton {
                    Layout.fillWidth: true
                    text: qsTr("Export…")
                    onClicked: backend.exportCounts()
                }
            }
        }
    }
}
