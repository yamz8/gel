import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The selected lane's profile from top to bottom of the gel: the signal,
// the background under it, and each band's measured area.
Rectangle {
    id: panel
    color: win.panel

    readonly property int laneIndex: backend.selectedLane >= 0 ? backend.selectedLane : 0
    readonly property var profile: (backend.lanes, backend.laneProfile(laneIndex))

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: win.line }

    Label {
        x: 12
        y: 8
        text: panel.profile.raw ? qsTr("Profile of lane %1").arg(panel.profile.label) : qsTr("Select a lane to see its profile.")
        font.pixelSize: 12
        color: win.dim
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        y: 8
        spacing: 14
        visible: !!panel.profile.raw
        Label { text: "— " + qsTr("signal"); color: "#e6e6ea"; font.pixelSize: 11 }
        Label { text: "— " + qsTr("background"); color: "#8b8bff"; font.pixelSize: 11 }
        Label { text: "■ " + qsTr("band volume"); color: theme.accent; font.pixelSize: 11 }
    }

    Canvas {
        id: plot
        anchors.fill: parent
        anchors.topMargin: 28
        anchors.margins: 12
        readonly property var profileData: panel.profile
        readonly property color accent: theme.accent
        onProfileDataChanged: requestPaint()
        onAccentChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const data = profileData
            const raw = data.raw, bg = data.background
            if (!raw || raw.length < 2)
                return
            let lo = Infinity, hi = -Infinity
            for (let i = 0; i < raw.length; ++i) {
                lo = Math.min(lo, raw[i], bg[i])
                hi = Math.max(hi, raw[i], bg[i])
            }
            if (hi <= lo) hi = lo + 1
            const w = width, h = height, n = raw.length
            const X = (i) => i / (n - 1) * w
            const Y = (v) => h - (v - lo) / (hi - lo) * (h - 4) - 2

            // Band areas: between the signal and the background.
            ctx.fillStyle = Qt.rgba(accent.r, accent.g, accent.b, 0.35)
            for (const band of data.bands) {
                const a = Math.max(0, Math.round(band.lower - data.top)), b = Math.min(n - 1, Math.round(band.upper - data.top))
                if (b <= a) continue
                ctx.beginPath()
                ctx.moveTo(X(a), Y(bg[a]))
                for (let i = a; i <= b; ++i) ctx.lineTo(X(i), Y(raw[i]))
                for (let i = b; i >= a; --i) ctx.lineTo(X(i), Y(bg[i]))
                ctx.closePath()
                ctx.fill()
            }
            ctx.lineWidth = 1
            ctx.strokeStyle = "#8b8bff"
            ctx.beginPath()
            for (let i = 0; i < n; ++i) i ? ctx.lineTo(X(i), Y(bg[i])) : ctx.moveTo(X(i), Y(bg[i]))
            ctx.stroke()
            ctx.lineWidth = 1.5
            ctx.strokeStyle = "#e6e6ea"
            ctx.beginPath()
            for (let i = 0; i < n; ++i) i ? ctx.lineTo(X(i), Y(raw[i])) : ctx.moveTo(X(i), Y(raw[i]))
            ctx.stroke()
            ctx.strokeStyle = accent
            ctx.lineWidth = 1
            for (const band of data.bands) {
                const x = X(band.position - data.top)
                ctx.beginPath()
                ctx.moveTo(x, 0)
                ctx.lineTo(x, h)
                ctx.stroke()
            }
        }
        // In band mode a click on the profile adds or removes a band there.
        MouseArea {
            anchors.fill: parent
            enabled: win.mode === "bands" && !!panel.profile.raw
            onClicked: (mouse) => {
                const n = panel.profile.raw.length
                backend.toggleBandAt(panel.laneIndex, panel.profile.top + mouse.x / width * (n - 1))
            }
        }
    }
}
