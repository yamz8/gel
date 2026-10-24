import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Ctrl+E: the format and size, then the portal's save dialog.
Popup {
    id: dialog
    objectName: "exportDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 24
    width: 400
    Material.roundedScale: theme.radius

    property string format: "png"
    property int dpi: 300
    property real widthMm: 85

    background: Rectangle { color: "#1b1b1f"; radius: theme.radius; border.color: win.line }

    function go() {
        backend.exportDialog(format, dpi, widthMm)
        close()
    }

    Shortcut { sequence: "Return"; enabled: dialog.visible; onActivated: dialog.go() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Label { text: qsTr("Export"); font.pixelSize: 18 }
        GridLayout {
            columns: 2
            columnSpacing: 12
            Repeater {
                model: [["png", qsTr("Figure, PNG")], ["svg", qsTr("Figure, SVG")],
                        ["pdf", qsTr("Figure, PDF")], ["csv", qsTr("Results, CSV")]]
                delegate: RadioButton {
                    required property var modelData
                    text: modelData[1]
                    checked: dialog.format === modelData[0]
                    onClicked: dialog.format = modelData[0]
                }
            }
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            enabled: dialog.format !== "csv"
            opacity: enabled ? 1 : 0.4
            Label { text: qsTr("Resolution") }
            SpinBox {
                from: 72; to: 2400; stepSize: 50
                value: dialog.dpi
                editable: true
                onValueModified: dialog.dpi = value
                textFromValue: (v, locale) => qsTr("%1 dpi").arg(Number(v).toLocaleString(locale, 'f', 0))
                valueFromText: (text, locale) => parseInt(text)
            }
            Label { text: qsTr("Width") }
            SpinBox {
                from: 20; to: 400; stepSize: 5
                value: dialog.widthMm
                editable: true
                onValueModified: dialog.widthMm = value
                textFromValue: (v, locale) => qsTr("%1 mm").arg(Number(v).toLocaleString(locale, 'f', 0))
                valueFromText: (text, locale) => parseInt(text)
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 11
            color: win.dim
            text: dialog.format === "csv"
                ? qsTr("One row per band: lane, label, size, raw and normalized volume.")
                : qsTr("85 mm fits one journal column, 175 mm two. The display settings are listed in the file's metadata.")
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); flat: true; onClicked: dialog.close() }
            Button { text: qsTr("Export…"); highlighted: true; onClicked: dialog.go() }
        }
    }
}
