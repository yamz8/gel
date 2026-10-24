import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Asks before unsaved work is thrown away.
Popup {
    id: dialog
    objectName: "confirmDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 24
    width: 420
    Material.roundedScale: theme.radius
    property var action: null

    function ask(then) {
        action = then
        open()
    }
    function finish(save) {
        const then = action
        action = null
        close()
        if (save && !backend.save())
            return
        if (then)
            then()
    }

    background: Rectangle { color: "#1b1b1f"; radius: theme.radius; border.color: win.line }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Label { text: qsTr("Save changes to %1?").arg(backend.fileName); font.pixelSize: 17; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label {
            text: qsTr("The lanes, bands, and annotations will be lost if you don't save them.")
            color: win.dim
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTr("Discard"); flat: true; onClicked: dialog.finish(false) }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); flat: true; onClicked: { dialog.action = null; dialog.close() } }
            Button { text: qsTr("Save"); highlighted: true; onClicked: dialog.finish(true) }
        }
    }
}
