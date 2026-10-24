import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: panel
    color: win.panel
    Material.roundedScale: theme.radius

    component Heading: Label {
        Layout.topMargin: 14
        font.pixelSize: 12
        font.bold: true
        font.capitalization: Font.AllUppercase
        color: win.dim
    }
    component Note: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 11
        color: win.dim
    }
    component Small: Button {
        focusPolicy: Qt.NoFocus
        flat: true
        font.pixelSize: 12
        Material.roundedScale: theme.radius
        Layout.preferredHeight: 36
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth - 28
            x: 14
            spacing: 6

            // ---- Display -------------------------------------------------
            Heading { text: qsTr("Display") }
            Note { text: qsTr("Only changes how the gel looks. Measurements always use the raw values.") }
            RowLayout {
                Layout.fillWidth: true
                Switch {
                    text: qsTr("Invert")
                    checked: backend.invert
                    focusPolicy: Qt.NoFocus
                    onToggled: backend.setInvert(checked)
                }
                Item { Layout.fillWidth: true }
                Small { text: qsTr("↺ 90°"); onClicked: backend.rotate(-1) }
                Small { text: qsTr("↻ 90°"); onClicked: backend.rotate(1) }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 8
                rowSpacing: 0

                Label { text: qsTr("Brightness"); font.pixelSize: 12 }
                Slider {
                    Layout.fillWidth: true
                    from: -1; to: 1; stepSize: 0.01
                    value: backend.brightness
                    focusPolicy: Qt.NoFocus
                    onMoved: backend.setBrightness(value)
                }
                Label { text: (translator.language, backend.brightness.toLocaleString(Qt.locale(), 'f', 2)); font.pixelSize: 12; Layout.preferredWidth: 40 }

                Label { text: qsTr("Contrast"); font.pixelSize: 12 }
                Slider {
                    Layout.fillWidth: true
                    from: 0.2; to: 5; stepSize: 0.01
                    value: backend.contrast
                    focusPolicy: Qt.NoFocus
                    onMoved: backend.setContrast(value)
                }
                Label { text: (translator.language, backend.contrast.toLocaleString(Qt.locale(), 'f', 2)); font.pixelSize: 12; Layout.preferredWidth: 40 }

                Label { text: qsTr("Straighten"); font.pixelSize: 12 }
                Slider {
                    Layout.fillWidth: true
                    from: -10; to: 10; stepSize: 0.05
                    value: backend.angle
                    focusPolicy: Qt.NoFocus
                    onMoved: backend.setAngle(value)
                }
                Label { text: (translator.language, backend.angle.toLocaleString(Qt.locale(), 'f', 2) + "°"); font.pixelSize: 12; Layout.preferredWidth: 40 }
            }
            RowLayout {
                Small { text: qsTr("Crop…"); onClicked: win.setMode("crop") }
                Small { text: qsTr("Reset crop"); enabled: backend.cropped; onClicked: backend.resetCrop() }
                Item { Layout.fillWidth: true }
                Small { text: qsTr("Reset view"); onClicked: { backend.setBrightness(0); backend.setContrast(1); backend.setInvert(false) } }
            }

            // ---- Lanes ---------------------------------------------------
            Heading { text: qsTr("Lanes") }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Bands are"); font.pixelSize: 12 }
                ComboBox {
                    Layout.fillWidth: true
                    focusPolicy: Qt.NoFocus
                    model: [backend.darkBands ? qsTr("Automatic: dark") : qsTr("Automatic: bright"),
                            qsTr("Bright on dark"), qsTr("Dark on light")]
                    currentIndex: backend.polarity
                    onActivated: (i) => backend.setPolarity(i)
                }
            }
            Note {
                visible: backend.lanes.length === 0
                text: qsTr("Press Space to find the lanes, or L to add one under the pointer.")
            }
            Repeater {
                model: backend.lanes
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 4
                    Rectangle {
                        Layout.preferredWidth: 4
                        Layout.preferredHeight: 28
                        color: backend.selectedLane === index ? theme.accent : "transparent"
                    }
                    Label { text: index + 1; color: win.dim; font.pixelSize: 12; Layout.preferredWidth: 18 }
                    TextField {
                        Layout.fillWidth: true
                        text: modelData.label
                        font.pixelSize: 13
                        selectByMouse: true
                        onEditingFinished: if (text !== modelData.label) backend.setLaneLabel(index, text)
                        onActiveFocusChanged: if (activeFocus) backend.select(index)
                    }
                    Small {
                        text: qsTr("Ladder")
                        checkable: true
                        checked: modelData.ladder
                        highlighted: modelData.ladder
                        onClicked: backend.toggleLadder(index)
                    }
                    Small {
                        text: "×"
                        Layout.preferredWidth: 32
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Remove the lane")
                        onClicked: backend.removeLane(index)
                    }
                }
            }

            // ---- Ladder --------------------------------------------------
            Heading { text: qsTr("Ladder") }
            ComboBox {
                id: preset
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
                model: backend.ladderPresets
                displayText: backend.ladderPreset
                currentIndex: backend.ladderPresets.indexOf(backend.ladderPreset)
                onActivated: (i) => backend.setLadderPreset(backend.ladderPresets[i])
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Sizes, largest first (%1)").arg(backend.ladderUnit)
                text: backend.ladderSizes
                font.pixelSize: 12
                selectByMouse: true
                onEditingFinished: if (text !== backend.ladderSizes) backend.setLadderSizes(text)
            }
            Note { text: backend.ladderFit }

            // ---- Quantification ------------------------------------------
            Heading { text: qsTr("Quantification") }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 2
                Label { text: qsTr("Background"); font.pixelSize: 12 }
                ComboBox {
                    Layout.fillWidth: true
                    focusPolicy: Qt.NoFocus
                    model: [qsTr("Rolling ball"), qsTr("Lane edges"), qsTr("None")]
                    currentIndex: backend.background
                    onActivated: (i) => backend.setBackground(i)
                }
                Label { text: qsTr("Ball radius"); font.pixelSize: 12; visible: backend.background === 0 }
                SpinBox {
                    Layout.fillWidth: true
                    visible: backend.background === 0
                    from: 1; to: 1000
                    value: backend.ballRadius
                    editable: true
                    onValueModified: backend.setBallRadius(value)
                    textFromValue: (v, locale) => qsTr("%1 px").arg(Number(v).toLocaleString(locale, 'f', 0))
                    valueFromText: (text, locale) => parseInt(text)
                }
                Label { text: qsTr("Normalize to"); font.pixelSize: 12 }
                ComboBox {
                    Layout.fillWidth: true
                    focusPolicy: Qt.NoFocus
                    model: [qsTr("Nothing"), qsTr("A reference lane"), qsTr("A reference band")]
                    currentIndex: backend.normalization
                    onActivated: (i) => backend.setNormalization(i)
                }
                Label { text: qsTr("Reference"); font.pixelSize: 12; visible: backend.normalization > 0 }
                RowLayout {
                    visible: backend.normalization > 0
                    Layout.fillWidth: true
                    ComboBox {
                        Layout.fillWidth: true
                        focusPolicy: Qt.NoFocus
                        model: backend.lanes.map((l, i) => qsTr("Lane %1").arg(l.label !== "" ? l.label : i + 1))
                        currentIndex: backend.referenceLane
                        onActivated: (i) => backend.setReferenceLane(i)
                    }
                    ComboBox {
                        visible: backend.normalization === 2
                        Layout.preferredWidth: 110
                        focusPolicy: Qt.NoFocus
                        readonly property var lane: backend.lanes[backend.referenceLane]
                        model: lane ? lane.bands.map((b, i) => qsTr("Band %1").arg(i + 1)) : []
                        currentIndex: backend.referenceBand
                        onActivated: (i) => backend.useAsReference(backend.referenceLane, i)
                    }
                }
            }
            Note { text: qsTr("Volume is the background-subtracted sum of the lane's pixel values under each band.") }

            // ---- Results -------------------------------------------------
            Heading { text: qsTr("Results") }
            Note { visible: backend.results.length === 0; text: qsTr("No bands yet.") }
            GridLayout {
                id: table
                visible: backend.results.length > 0
                Layout.fillWidth: true
                columns: 5
                columnSpacing: 10
                rowSpacing: 0
                Repeater {
                    model: [qsTr("Lane"), qsTr("Band"), qsTr("Size"), qsTr("Volume"), qsTr("Normalized")]
                    delegate: Label {
                        required property string modelData
                        required property int index
                        text: modelData
                        font.pixelSize: 11
                        font.bold: true
                        color: win.dim
                        Layout.fillWidth: index === 3
                        horizontalAlignment: index >= 2 ? Text.AlignRight : Text.AlignLeft
                    }
                }
                Repeater {
                    model: backend.results.length * 5
                    delegate: Label {
                        required property int index
                        readonly property var row: backend.results[Math.floor(index / 5)]
                        readonly property int column: index % 5
                        readonly property bool selected: row && backend.selectedLane === row.lane && backend.selectedBand === row.band
                        Layout.fillWidth: column === 3
                        horizontalAlignment: column >= 2 ? Text.AlignRight : Text.AlignLeft
                        font.pixelSize: 12
                        font.features: { "tnum": 1 }
                        topPadding: 3
                        bottomPadding: 3
                        elide: Text.ElideRight
                        Layout.maximumWidth: column === 0 ? 90 : -1
                        color: !row ? "white" : selected ? theme.accent : row.saturated && column === 3 ? "#ff7b72"
                             : row.ladder ? win.dim : "#e6e6ea"
                        text: !row ? "" : column === 0 ? row.label
                            : column === 1 ? (row.band + 1) + (row.reference ? " ★" : "")
                            : column === 2 ? row.size
                            : column === 3 ? row.volume + (row.saturated ? " !" : "")
                            : row.normalized
                        MouseArea {
                            anchors.fill: parent
                            onClicked: backend.select(parent.row.lane, parent.row.band)
                            onDoubleClicked: backend.useAsReference(parent.row.lane, parent.row.band)
                        }
                    }
                }
            }
            Note {
                visible: backend.results.length > 0
                text: qsTr("Double-click a row to make that band the reference. ~ marks a size outside the ladder, ! a band with saturated pixels.")
            }
            Repeater {
                model: backend.warnings
                delegate: Label {
                    required property string modelData
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 11
                    color: "#e3b341"
                    text: modelData
                }
            }
            Item { Layout.preferredHeight: 16 }
        }
    }
}
