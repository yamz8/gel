import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    implicitHeight: 28
    color: win.panel

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: win.line }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 16

        Label {
            Layout.fillWidth: true
            text: backend.status
            color: backend.statusIsError ? "#ff7b72" : win.dim
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Label {
            visible: win.mode !== "normal"
            text: win.mode === "bands" ? qsTr("Click in a lane to add or remove a band")
                : win.mode === "arrow" ? qsTr("Drag from the tail to the head of the arrow")
                : win.mode === "text" ? qsTr("Click where the label goes")
                : qsTr("Drag the area to keep")
            color: theme.accent
            font.pixelSize: 12
        }
        Label {
            visible: backend.hasImage && win.pointerInside
            readonly property real raw: backend.rawAt(win.pointer.x, win.pointer.y)
            text: qsTr("x %1  y %2  value %3")
                .arg(Math.round(win.pointer.x)).arg(Math.round(win.pointer.y))
                .arg(isNaN(raw) ? "–" : Math.round(raw).toLocaleString(Qt.locale(), 'f', 0))
            color: win.dim
            font.pixelSize: 12
            font.features: { "tnum": 1 }
        }
    }
}
