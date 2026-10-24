import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1360
    height: 860
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: backend.hasImage ? (backend.dirty ? "• " : "") + backend.fileName + " — Gel" : "Gel"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    // normal, bands, arrow, text, or crop: what a click on the gel does.
    property string mode: "normal"
    property bool profileVisible: true
    property bool helpVisible: false
    // Where the pointer is over the gel, in frame pixels.
    property point pointer: Qt.point(0, 0)
    property bool pointerInside: false
    readonly property color panel: "#17171a"
    readonly property color line: "#2a2a2f"
    readonly property color dim: "#9a9aa2"
    // Typing in a field must not trigger the one-letter shortcuts.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.cursorVisible !== undefined
    readonly property bool overlay: helpVisible || exportDialog.visible || confirm.visible || gelView.editing

    function setMode(m) { mode = mode === m ? "normal" : m }

    // Runs action now, or after asking, when there is unsaved work.
    function guard(action) {
        if (backend.dirty)
            confirm.ask(action)
        else
            action()
    }

    property bool closeConfirmed: false
    onClosing: (close) => {
        if (backend.dirty && !closeConfirmed) {
            close.accepted = false
            confirm.ask(() => { win.closeConfirmed = true; win.close() })
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.overlay; onActivated: win.close() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (gelView.editing) gelView.cancelEdit()
            else if (win.mode !== "normal") win.mode = "normal"
            else backend.clearSelection()
        }
    }
    Shortcut { sequences: [StandardKey.Open]; context: Qt.ApplicationShortcut; onActivated: win.guard(() => backend.openDialog()) }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; onActivated: backend.save() }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; enabled: backend.hasImage; onActivated: exportDialog.open() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.redo() }

    // One-letter keys act on the gel, so not while typing or behind a dialog.
    readonly property bool keys: backend.hasImage && !win.typing && !win.overlay
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.detect(false) }
    Shortcut { sequence: "Shift+Space"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.detect(true) }
    Shortcut { sequence: "L"; context: Qt.ApplicationShortcut; enabled: win.keys
        onActivated: backend.addLane(win.pointerInside ? win.pointer.x : backend.crop.x + backend.crop.width / 2) }
    Shortcut { sequence: "B"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: win.setMode("bands") }
    Shortcut { sequence: "A"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: win.setMode("arrow") }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: win.setMode("text") }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: win.setMode("crop") }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: win.profileVisible = !win.profileVisible }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.setInvert(!backend.invert) }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.rotate(1) }
    Shortcut { sequence: "Shift+R"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.rotate(-1) }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.setAngle(backend.angle - 0.25) }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.setAngle(backend.angle + 0.25) }
    Shortcut { sequences: ["Delete", "Backspace"]; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.removeSelected() }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.nudge(-1, 0) }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.nudge(1, 0) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.nudge(0, -1) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.nudge(0, 1) }
    Shortcut { sequence: "Shift+Left"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.nudge(-10, 0) }
    Shortcut { sequence: "Shift+Right"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.nudge(10, 0) }
    Shortcut { sequence: "Shift+Up"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.nudge(0, -10) }
    Shortcut { sequence: "Shift+Down"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.nudge(0, 10) }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: root
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Toolbar {
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    GelView {
                        id: gelView
                        objectName: "gelView"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    ProfilePanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 190
                        visible: win.profileVisible && backend.hasImage
                    }
                }

                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    color: win.line
                    visible: backend.hasImage
                }

                SidePanel {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 380
                    visible: backend.hasImage
                }
            }

            StatusBar {
                Layout.fillWidth: true
            }
        }

        DropArea {
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: (drop) => {
                if (drop.hasUrls) {
                    const url = drop.urls[0]
                    win.guard(() => backend.openUrl(url))
                }
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
        }
    }

    ExportDialog { id: exportDialog }
    ConfirmDialog { id: confirm }
}
