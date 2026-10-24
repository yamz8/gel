import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// One lane on the gel, with its bands. Dragging moves it and its edges
// resize it; the change is committed on release, so the drag isn't lost
// when the lanes are rebuilt.
Item {
    id: box
    property var lane
    property int laneIndex
    property real s: 1
    readonly property bool selected: backend.selectedLane === laneIndex
    readonly property bool interactive: win.mode === "normal"

    // The live geometry while dragging, in frame pixels.
    property real dx: 0
    property real dy: 0
    property real dLeft: 0
    property real dRight: 0
    property real dTop: 0
    property real dBottom: 0
    readonly property real laneLeft: lane.center - lane.width / 2 + dx + dLeft
    readonly property real laneRight: lane.center + lane.width / 2 + dx + dRight
    readonly property real laneTop: lane.top + dy + dTop
    readonly property real laneBottom: lane.bottom + dy + dBottom

    x: laneLeft * s
    y: laneTop * s
    width: Math.max(2, (laneRight - laneLeft) * s)
    height: Math.max(2, (laneBottom - laneTop) * s)

    function commit() {
        backend.setLaneGeometry(laneIndex, (laneLeft + laneRight) / 2, laneRight - laneLeft, laneTop, laneBottom)
        dx = dy = dLeft = dRight = dTop = dBottom = 0
    }

    Rectangle {
        anchors.fill: parent
        color: box.selected ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
        border.width: box.selected ? 2 : 1
        border.color: box.selected ? theme.accent : lane.ladder ? Qt.rgba(1, 1, 1, 0.75) : Qt.rgba(1, 1, 1, 0.38)
        radius: Math.min(2, theme.radius)
    }

    // Moving the whole lane.
    MouseArea {
        anchors.fill: parent
        enabled: box.interactive
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        preventStealing: true
        property point start
        onPressed: (mouse) => {
            start = mapToItem(box.parent, mouse.x, mouse.y)
            backend.select(box.laneIndex)
        }
        onPositionChanged: (mouse) => {
            const p = mapToItem(box.parent, mouse.x, mouse.y)
            box.dx = (p.x - start.x) / box.s
            box.dy = (p.y - start.y) / box.s
        }
        onReleased: if (box.dx !== 0 || box.dy !== 0) box.commit()
    }

    Repeater {
        model: lane.bands
        delegate: Item {
            id: mark
            required property var modelData
            required property int index
            readonly property bool selected: box.selected && backend.selectedBand === index
            x: 0
            width: box.width
            y: (modelData.lower - box.lane.top) * box.s
            height: Math.max(3, (modelData.upper - modelData.lower + 1) * box.s)

            Rectangle {
                anchors.fill: parent
                color: theme.accent
                opacity: mark.selected ? 0.32 : 0.14
            }
            Rectangle {
                width: parent.width
                height: mark.selected ? 2 : 1
                y: (mark.modelData.position - mark.modelData.lower) * box.s - height / 2
                color: mark.modelData.saturated ? "#ff5f56" : theme.accent
            }
            // Estimated sizes show on the lane being looked at.
            Label {
                visible: box.selected && mark.modelData.size !== "" && !box.lane.ladder
                anchors.left: parent.right
                anchors.leftMargin: 4
                y: (mark.modelData.position - mark.modelData.lower) * box.s - height / 2
                text: mark.modelData.size
                font.pixelSize: 11
                color: "#f2f2f4"
                padding: 1
                leftPadding: 3
                rightPadding: 3
                background: Rectangle { color: Qt.rgba(0, 0, 0, 0.6); radius: Math.min(3, theme.radius) }
            }
            MouseArea {
                anchors.fill: parent
                enabled: box.interactive
                onClicked: backend.select(box.laneIndex, mark.index)
            }
        }
    }

    // Edge handles: sides set the width, top and bottom the extent.
    component Handle: MouseArea {
        property string edge
        enabled: box.interactive
        preventStealing: true
        property point start
        cursorShape: edge === "left" || edge === "right" ? Qt.SizeHorCursor : Qt.SizeVerCursor
        onPressed: (mouse) => {
            start = mapToItem(box.parent, mouse.x, mouse.y)
            backend.select(box.laneIndex)
        }
        onPositionChanged: (mouse) => {
            const p = mapToItem(box.parent, mouse.x, mouse.y)
            const ddx = (p.x - start.x) / box.s, ddy = (p.y - start.y) / box.s
            if (edge === "left") { box.dLeft = ddx; box.dRight = -ddx }
            else if (edge === "right") { box.dRight = ddx; box.dLeft = -ddx }
            else if (edge === "top") box.dTop = ddy
            else box.dBottom = ddy
        }
        onReleased: box.commit()
    }
    Handle { edge: "left"; x: -4; width: 8; height: parent.height }
    Handle { edge: "right"; x: parent.width - 4; width: 8; height: parent.height }
    Handle { edge: "top"; y: -4; height: 8; width: parent.width }
    Handle { edge: "bottom"; y: parent.height - 4; height: 8; width: parent.width }
}
