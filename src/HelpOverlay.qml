import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, on ?.
Rectangle {
    color: Qt.rgba(0, 0, 0, 0.72)
    MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, grid.implicitWidth + 64)
        height: Math.min(parent.height - 48, column.implicitHeight + 48)
        color: "#1b1b1f"
        radius: theme.radius
        border.color: win.line

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14
            Label { text: qsTr("Keyboard shortcuts"); font.pixelSize: 18 }
            GridLayout {
                id: grid
                columns: 4
                columnSpacing: 18
                rowSpacing: 6
                Repeater {
                    model: [
                        ["Space", qsTr("Find lanes and bands, and quantify")],
                        ["Shift+Space", qsTr("Find the lanes again too")],
                        ["L", qsTr("Add a lane under the pointer")],
                        ["B", qsTr("Pick bands: click to add or remove")],
                        ["P", qsTr("Show or hide the lane profile")],
                        ["I", qsTr("Invert the display")],
                        ["R / Shift+R", qsTr("Turn 90° clockwise / counterclockwise")],
                        ["[ / ]", qsTr("Straighten by a quarter degree")],
                        ["C", qsTr("Crop: drag the area to keep")],
                        ["A", qsTr("Draw an arrow")],
                        ["T", qsTr("Place a text label")],
                        [qsTr("Arrows"), qsTr("Nudge the selection (Shift: 10 px)")],
                        [qsTr("Delete"), qsTr("Remove the selection")],
                        ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo / redo")],
                        ["Ctrl+O", qsTr("Open an image or session")],
                        ["Ctrl+S", qsTr("Save the session next to the image")],
                        ["Ctrl+E", qsTr("Export the figure or the results")],
                        ["Esc", qsTr("Leave the current mode")],
                        ["?", qsTr("Show this list")],
                        ["Q", qsTr("Quit")]
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.columnSpan: 2
                        spacing: 12
                        Label {
                            text: modelData[0]
                            font.family: "monospace"
                            font.pixelSize: 12
                            color: theme.accent
                            Layout.preferredWidth: 150
                        }
                        Label { text: modelData[1]; font.pixelSize: 13; Layout.preferredWidth: 270; wrapMode: Text.Wrap }
                    }
                }
            }
        }
    }
}
