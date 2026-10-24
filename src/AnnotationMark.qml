import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// An arrow or a text label on the gel. Drag to move; double-click a label
// to change it.
Item {
    id: mark
    property var note
    property int noteIndex
    property real s: 1
    signal editRequested()
    readonly property bool selected: backend.selectedAnnotation === noteIndex
    readonly property color ink: backend.annotationColor
    // Drawn at the size the figure prints them: 8 pt text, 1 pt arrows with
    // a 4.5 pt head, at the width chosen for export.
    readonly property real pt: (backend.lanes, backend.pixelsPerPoint(exportDialog.widthMm)) * s

    property real dx: 0
    property real dy: 0
    readonly property real x1: (note.x + dx) * s
    readonly property real y1: (note.y + dy) * s
    readonly property real x2: (note.toX + dx) * s
    readonly property real y2: (note.toY + dy) * s

    anchors.fill: parent

    // Arrow: shaft and a filled head, scaled like the figure's.
    Shape {
        id: arrow
        visible: mark.note.arrow
        anchors.fill: parent
        readonly property real len: Math.max(1, Math.hypot(mark.x2 - mark.x1, mark.y2 - mark.y1))
        readonly property real ux: (mark.x2 - mark.x1) / len
        readonly property real uy: (mark.y2 - mark.y1) / len
        readonly property real head: Math.min(len * 0.6, 4.5 * mark.pt)
        ShapePath {
            strokeColor: mark.selected ? theme.accent : mark.ink
            strokeWidth: Math.max(1, mark.pt)
            capStyle: ShapePath.RoundCap
            startX: mark.x1; startY: mark.y1
            PathLine { x: mark.x2 - arrow.ux * arrow.head * 0.8; y: mark.y2 - arrow.uy * arrow.head * 0.8 }
        }
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillColor: mark.selected ? theme.accent : mark.ink
            startX: mark.x2; startY: mark.y2
            PathLine { x: mark.x2 - arrow.ux * arrow.head - arrow.uy * arrow.head * 0.45
                       y: mark.y2 - arrow.uy * arrow.head + arrow.ux * arrow.head * 0.45 }
            PathLine { x: mark.x2 - arrow.ux * arrow.head + arrow.uy * arrow.head * 0.45
                       y: mark.y2 - arrow.uy * arrow.head - arrow.ux * arrow.head * 0.45 }
            PathLine { x: mark.x2; y: mark.y2 }
        }
    }

    Label {
        id: label
        visible: !mark.note.arrow
        x: mark.x1
        y: mark.y1
        text: mark.note.text
        color: mark.ink
        font.pixelSize: Math.max(6, Math.round(8 * mark.pt))
        font.family: "Liberation Sans"
        topPadding: 0
        bottomPadding: 0
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            color: "transparent"
            border.color: theme.accent
            visible: mark.selected
        }
    }

    // What takes the clicks: the label, or a box around the arrow's head.
    MouseArea {
        enabled: win.mode === "normal"
        x: mark.note.arrow ? Math.min(mark.x1, mark.x2) - 6 : label.x - 3
        y: mark.note.arrow ? Math.min(mark.y1, mark.y2) - 6 : label.y - 3
        width: mark.note.arrow ? Math.abs(mark.x2 - mark.x1) + 12 : label.width + 6
        height: mark.note.arrow ? Math.abs(mark.y2 - mark.y1) + 12 : label.height + 6
        cursorShape: Qt.SizeAllCursor
        preventStealing: true
        property point start
        onPressed: (mouse) => {
            start = mapToItem(mark, mouse.x, mouse.y)
            backend.selectAnnotation(mark.noteIndex)
        }
        onPositionChanged: (mouse) => {
            const p = mapToItem(mark, mouse.x, mouse.y)
            mark.dx = (p.x - start.x) / mark.s
            mark.dy = (p.y - start.y) / mark.s
        }
        onReleased: {
            if (mark.dx !== 0 || mark.dy !== 0)
                backend.moveAnnotation(mark.noteIndex, mark.dx, mark.dy)
            mark.dx = mark.dy = 0
        }
        onDoubleClicked: if (!mark.note.arrow) mark.editRequested()
    }
}
