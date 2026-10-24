// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import VLC.MainInterface
import VLC.Player
import VLC.Style
import VLC.Widgets as Widgets

// Presentation only: all actions and message contents still come from VLC.
Rectangle {
    id: root
    property string message: ""
    property string symbol: ""
    property real progress: -1
    property bool seekFeedback: false

    width: Math.min(parent.width - VLCStyle.dp(32, VLCStyle.scale),
                    Math.max(VLCStyle.dp(180, VLCStyle.scale), content.implicitWidth + 40))
    height: content.implicitHeight + VLCStyle.dp(28, VLCStyle.scale)
    radius: VLCStyle.redesign.radius
    color: VLCStyle.redesign.surface
    border.width: VLCStyle.dp(1, VLCStyle.scale)
    border.color: VLCStyle.redesign.accent
    visible: lifetime.running
    // No input handlers: the notification must not intercept video gestures.
    Accessible.role: Accessible.StaticText
    Accessible.name: message

    ColumnLayout {
        id: content
        anchors.centerIn: parent
        width: parent.width - VLCStyle.dp(32, VLCStyle.scale)
        spacing: VLCStyle.dp(10, VLCStyle.scale)
        RowLayout {
            Layout.fillWidth: true
            spacing: VLCStyle.dp(12, VLCStyle.scale)
            Text {
                visible: root.symbol !== ""
                text: root.symbol
                font.family: VLCIcons.fontFamily
                font.pixelSize: VLCStyle.dp(24, VLCStyle.scale)
                color: VLCStyle.redesign.accent
            }
            Text {
                font.pixelSize: VLCStyle.dp(14, VLCStyle.scale)
                Layout.fillWidth: true
                text: root.message
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: VLCStyle.redesign.foreground
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: VLCStyle.dp(4, VLCStyle.scale)
            visible: root.progress >= 0
            radius: height / 2
            color: VLCStyle.redesign.track
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, root.progress))
                height: parent.height
                radius: height / 2
                color: VLCStyle.redesign.accent
            }
        }
    }

    Timer { id: lifetime; interval: 1200 }

    Connections {
        target: Player
        function onSeekJumped(seconds) {
            root.seekFeedback = true
            root.message = seconds > 0 ? qsTr("+%1s").arg(seconds) : qsTr("−%1s").arg(-seconds)
            root.symbol = ""
            root.progress = -1
            lifetime.interval = 1200
            lifetime.restart()
        }
        function onOsdRequested(text, type, value, duration) {
            // Paused seeks emit position and frame-step OSD messages as well.
            // Keep the explicit jump feedback visible for its full lifetime.
            if (root.seekFeedback && lifetime.running
                && (type === 5 || (type === 0
                    && (text === qsTr("Next frame") || text === qsTr("Previous frame")
                        || /^\d+:\d+/.test(text)))))
                return
            root.seekFeedback = false
            root.symbol = ""
            root.progress = -1
            switch (type) {
            case 1: root.message = qsTr("Playing"); root.symbol = VLCIcons.play_filled; break
            case 2: root.message = qsTr("Paused"); root.symbol = VLCIcons.pause_filled; break
            case 3: root.message = qsTr("Sound on"); root.symbol = VLCIcons.volume_high; break
            case 4: root.message = qsTr("Muted"); root.symbol = VLCIcons.volume_muted; break
            case 5:
                root.message = qsTr("Position: %1%").arg(value)
                root.progress = value / 100
                break
            case 6:
                root.message = qsTr("Volume: %1%").arg(value)
                root.symbol = VLCIcons.volume_high
                root.progress = value / 200
                break
            default: root.message = text; break
            }
            lifetime.interval = Math.max(1200, duration)
            lifetime.restart()
        }
    }
}
