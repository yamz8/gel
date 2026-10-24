import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    implicitHeight: 48
    color: win.panel
    Material.roundedScale: theme.radius

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }

    component Tool: ToolButton {
        property string key
        property string tip
        focusPolicy: Qt.NoFocus
        font.pixelSize: 13
        ToolTip.visible: hovered && (tip !== "" || key !== "")
        ToolTip.delay: 500
        ToolTip.text: key === "" ? tip : tip + "  (" + key + ")"
    }

    component Divider: Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: win.line }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 2

        Tool { text: qsTr("Open"); key: "Ctrl+O"; tip: qsTr("Open an image or session"); onClicked: win.guard(() => backend.openDialog()) }
        Tool { text: qsTr("Save"); key: "Ctrl+S"; tip: qsTr("Save the session next to the image"); enabled: backend.hasImage; onClicked: backend.save() }
        Tool { text: qsTr("Export"); key: "Ctrl+E"; tip: qsTr("Export the figure or the results"); enabled: backend.hasImage; onClicked: exportDialog.open() }
        Divider {}
        Tool {
            text: qsTr("Detect"); key: qsTr("Space"); tip: qsTr("Find lanes and bands, and quantify")
            enabled: backend.hasImage; highlighted: true; onClicked: backend.detect(false)
        }
        Tool { text: qsTr("Lane"); key: "L"; tip: qsTr("Add a lane under the pointer"); enabled: backend.hasImage
            onClicked: backend.addLane(backend.crop.x + backend.crop.width / 2) }
        Tool { text: qsTr("Bands"); key: "B"; tip: qsTr("Click in a lane to add or remove a band"); enabled: backend.hasImage
            checkable: true; checked: win.mode === "bands"; onClicked: win.setMode("bands") }
        Tool { text: qsTr("Arrow"); key: "A"; tip: qsTr("Drag to draw an arrow"); enabled: backend.hasImage
            checkable: true; checked: win.mode === "arrow"; onClicked: win.setMode("arrow") }
        Tool { text: qsTr("Text"); key: "T"; tip: qsTr("Click to place a label"); enabled: backend.hasImage
            checkable: true; checked: win.mode === "text"; onClicked: win.setMode("text") }
        Tool { text: qsTr("Crop"); key: "C"; tip: qsTr("Drag a rectangle to crop the view"); enabled: backend.hasImage
            checkable: true; checked: win.mode === "crop"; onClicked: win.setMode("crop") }
        Divider {}
        Tool { text: qsTr("Profile"); key: "P"; tip: qsTr("Show the lane profile"); enabled: backend.hasImage
            checkable: true; checked: win.profileVisible; onClicked: win.profileVisible = !win.profileVisible }
        Divider {}
        Tool { text: qsTr("Undo"); key: "Ctrl+Z"; enabled: backend.canUndo; onClicked: backend.undo() }
        Tool { text: qsTr("Redo"); key: "Ctrl+Shift+Z"; enabled: backend.canRedo; onClicked: backend.redo() }

        Item { Layout.fillWidth: true }

        Label {
            text: backend.imageInfo
            color: win.dim
            font.pixelSize: 12
            elide: Text.ElideMiddle
            Layout.maximumWidth: 260
        }
        Tool { text: "?"; tip: qsTr("Keyboard shortcuts"); key: "?"; onClicked: win.helpVisible = !win.helpVisible }
    }
}
