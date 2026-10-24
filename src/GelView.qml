import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Shapes

// The gel, fitted to the space, with its lanes, bands, ladder sizes, and
// annotations on top. Everything inside `content` is laid out in frame pixels
// times `s`, so it lines up with the image at any size.
Item {
    id: view
    clip: true

    // In crop mode the whole frame shows, so a new crop can reach anywhere.
    readonly property rect area: win.mode === "crop" ? Qt.rect(0, 0, backend.frameWidth, backend.frameHeight) : backend.crop
    readonly property real labelSpace: 26
    readonly property real sideSpace: 64
    readonly property real s: backend.hasImage && area.width > 0
        ? Math.max(0.01, Math.min((width - 2 * sideSpace) / area.width, (height - labelSpace - 24) / area.height)) : 1
    readonly property bool editing: editor.visible
    // Ladder sizes that have room for their label at this zoom.
    readonly property var shownMarks: {
        const out = []
        let last = -1e9
        for (const mark of backend.ladderMarks)
            if ((mark.y - last) * s >= 14) {
                out.push(mark)
                last = mark.y
            }
        return out
    }

    function toFrame(px, py) {
        return Qt.point(area.x + (px - gel.x) / s, area.y + (py - gel.y) / s)
    }
    function fromFrame(fx, fy) {
        return Qt.point(gel.x + (fx - area.x) * s, gel.y + (fy - area.y) * s)
    }

    // One small text field for lane labels and text annotations, opened
    // where the text goes.
    function edit(px, py, text, done) {
        editor.done = done
        editor.text = text
        editor.x = Math.min(Math.max(4, px), width - editor.width - 4)
        editor.y = Math.min(Math.max(4, py), height - editor.height - 4)
        editor.visible = true
        editor.forceActiveFocus()
        editor.selectAll()
    }
    function cancelEdit() { editor.visible = false; editor.done = null }

    Rectangle { anchors.fill: parent; color: "#0b0b0d" }

    // Nothing open yet.
    ColumnLayout {
        anchors.centerIn: parent
        visible: !backend.hasImage
        spacing: 14
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Open a gel or blot image")
            font.pixelSize: 22
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("PNG, JPEG, or TIFF, 8 or 16 bits. Drop it here, or press Ctrl+O.")
            color: win.dim
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Open…")
            highlighted: true
            focusPolicy: Qt.NoFocus
            Material.roundedScale: theme.radius
            onClicked: backend.openDialog()
        }
    }

    Item {
        id: gel
        objectName: "gel"
        visible: backend.hasImage
        width: view.area.width * view.s
        height: view.area.height * view.s
        x: Math.round((view.width - width) / 2)
        y: Math.round(view.labelSpace + Math.max(0, (view.height - view.labelSpace - 12 - height) / 2))
        clip: true

        Item {
            id: content
            x: -view.area.x * view.s
            y: -view.area.y * view.s
            width: backend.frameWidth * view.s
            height: backend.frameHeight * view.s

            Image {
                anchors.fill: parent
                source: backend.hasImage ? "image://gel/" + backend.imageRevision : ""
                cache: false
                smooth: view.s < 2
                mipmap: view.s < 1
                fillMode: Image.Stretch
            }

            // Clicks on the gel itself, by mode. Lanes and annotations lie
            // above and take their own clicks in normal mode.
            MouseArea {
                id: canvas
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                cursorShape: win.mode === "normal" ? Qt.ArrowCursor : Qt.CrossCursor
                property point start
                property point current
                property bool dragging: false
                function frame(mouse) { return Qt.point(mouse.x / view.s, mouse.y / view.s) }
                onPressed: (mouse) => {
                    start = frame(mouse)
                    current = start
                    dragging = win.mode === "arrow" || win.mode === "crop"
                }
                onPositionChanged: (mouse) => { if (dragging) current = frame(mouse) }
                onReleased: (mouse) => {
                    const end = frame(mouse)
                    if (win.mode === "arrow" && dragging) {
                        backend.addArrow(start.x, start.y, end.x, end.y)
                        win.mode = "normal"
                    } else if (win.mode === "crop" && dragging) {
                        backend.setCrop(Math.min(start.x, end.x), Math.min(start.y, end.y),
                                        Math.abs(end.x - start.x), Math.abs(end.y - start.y))
                        win.mode = "normal"
                    }
                    dragging = false
                }
                onClicked: (mouse) => {
                    const p = frame(mouse)
                    if (win.mode === "bands") {
                        const lane = backend.laneAt(p.x)
                        if (lane >= 0)
                            backend.toggleBandAt(lane, p.y)
                    } else if (win.mode === "text") {
                        const at = view.mapFromItem(content, mouse.x, mouse.y)
                        view.edit(at.x, at.y - 18, "", (text) => backend.addText(p.x, p.y, text))
                        win.mode = "normal"
                    } else if (win.mode === "normal") {
                        backend.clearSelection()
                    }
                }
            }

            Repeater {
                model: backend.lanes
                delegate: LaneBox {
                    required property var modelData
                    required property int index
                    lane: modelData
                    laneIndex: index
                    s: view.s
                }
            }

            Repeater {
                model: backend.annotations
                delegate: AnnotationMark {
                    required property var modelData
                    required property int index
                    note: modelData
                    noteIndex: index
                    s: view.s
                    onEditRequested: {
                        const at = view.fromFrame(modelData.x, modelData.y)
                        view.edit(at.x, at.y - 4, modelData.text, (text) => backend.setAnnotationText(index, text))
                    }
                }
            }

            // The arrow or crop being dragged out.
            Shape {
                anchors.fill: parent
                visible: canvas.dragging && win.mode === "arrow"
                ShapePath {
                    strokeColor: theme.accent
                    strokeWidth: 2
                    startX: canvas.start.x * view.s; startY: canvas.start.y * view.s
                    PathLine { x: canvas.current.x * view.s; y: canvas.current.y * view.s }
                }
            }
            Rectangle {
                visible: canvas.dragging && win.mode === "crop"
                x: Math.min(canvas.start.x, canvas.current.x) * view.s
                y: Math.min(canvas.start.y, canvas.current.y) * view.s
                width: Math.abs(canvas.current.x - canvas.start.x) * view.s
                height: Math.abs(canvas.current.y - canvas.start.y) * view.s
                color: "transparent"
                border.color: theme.accent
                border.width: 2
            }
            // The kept area, while choosing a new one.
            Rectangle {
                visible: win.mode === "crop" && backend.cropped && !canvas.dragging
                x: backend.crop.x * view.s; y: backend.crop.y * view.s
                width: backend.crop.width * view.s; height: backend.crop.height * view.s
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.6)
                border.width: 1
            }
        }

        HoverHandler {
            onPointChanged: {
                const p = point.position
                win.pointer = Qt.point(view.area.x + p.x / view.s, view.area.y + p.y / view.s)
            }
            onHoveredChanged: win.pointerInside = hovered
        }
    }

    // Lane labels above the gel; double-click one to rename it.
    Repeater {
        model: backend.lanes
        delegate: Label {
            required property var modelData
            required property int index
            readonly property point at: view.fromFrame(modelData.center, 0)
            visible: backend.hasImage && modelData.center >= view.area.x && modelData.center <= view.area.x + view.area.width
            x: at.x - width / 2
            y: gel.y - height - 6
            width: Math.max(18, Math.min(implicitWidth, modelData.width * view.s * 1.6))
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: modelData.label
            font.pixelSize: 12
            font.bold: backend.selectedLane === index
            color: modelData.ladder ? theme.accent : "#e6e6ea"
            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                onClicked: backend.select(parent.index)
                onDoubleClicked: view.edit(parent.x - 30, parent.y - 6, parent.modelData.label,
                                           (text) => backend.setLaneLabel(parent.index, text))
            }
        }
    }

    // Marker sizes beside the ladder lane.
    Repeater {
        model: backend.ladderLane >= 0 ? view.shownMarks : []
        delegate: Label {
            required property var modelData
            readonly property var ladder: backend.lanes[backend.ladderLane]
            readonly property point at: view.fromFrame(ladder ? ladder.center - ladder.width / 2 : 0, modelData.y)
            visible: backend.hasImage && modelData.y >= view.area.y && modelData.y <= view.area.y + view.area.height
            x: Math.max(2, at.x - width - 4)
            y: at.y - height / 2
            text: modelData.text
            font.pixelSize: 11
            color: "#f2f2f4"
            padding: 1
            leftPadding: 3
            rightPadding: 3
            background: Rectangle { color: Qt.rgba(0, 0, 0, 0.6); radius: Math.min(3, theme.radius) }
        }
    }

    TextField {
        id: editor
        objectName: "editor"
        visible: false
        width: 200
        property var done: null
        Material.roundedScale: theme.radius
        font.pixelSize: 13
        background: Rectangle { color: "#202024"; border.color: theme.accent; radius: Math.min(4, theme.radius) }
        onAccepted: {
            const action = done
            visible = false
            done = null
            if (action)
                action(text)
        }
        onActiveFocusChanged: if (!activeFocus && visible) accepted()
    }
}
