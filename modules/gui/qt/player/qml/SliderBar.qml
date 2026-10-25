/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
import QtQuick
import QtQuick.Templates as T
import QtQuick.Layouts

import VLC.Player
import VLC.Widgets as Widgets
import VLC.Style
import VLC.Util
import VLC.MainInterface

T.ProgressBar {
    id: control

    property bool rectangular: false

    readonly property real _hoveredScalingFactor: rectangular ? 1.0 : 1.8
    property int barHeight: VLCStyle.dp(5, VLCStyle.scale)
    readonly property real _scaledBarHeight: control.barHeight * _hoveredScalingFactor
    readonly property real _scaledY: (-control.barHeight / 2) * (control._hoveredScalingFactor - 1)

    property bool _isSeekPointsShown: true
    readonly property int _seekPointsDistance: VLCStyle.dp(2, VLCStyle.scale)
    readonly property int _seekPointsRadius: VLCStyle.dp(0.5, VLCStyle.scale)
    readonly property real _scaledSeekPointsRadius: _seekPointsRadius * _hoveredScalingFactor

    property bool _currentChapterHovered: false

    property color backgroundColor: theme.bg.primary
    property real touchHandlerMargin: VLCStyle.touchHandlerMargin

    Keys.onRightPressed: Player.jumpSeconds(10)
    Keys.onLeftPressed: Player.jumpSeconds(-10)

    Accessible.onIncreaseAction: Player.jumpFwd()
    Accessible.onDecreaseAction: Player.jumpBwd()

    function showChapterMarks() {
        _isSeekPointsShown = true
        seekpointTimer.restart()
    }

    readonly property ColorContext colorContext: ColorContext {
        id: theme
        colorSet: ColorContext.Slider

        enabled: control.enabled
        focused: control.visualFocus
        hovered: hoverHandler.hovered
    }

    Timer {
        id: seekpointTimer
        running: Player.hasChapters && !hoverHandler.hovered && _isSeekPointsShown
        interval: 3000
        onTriggered: control._isSeekPointsShown = false
    }

    Widgets.PointingToolTipAttached.visible: false

    SeekPreview {
        id: preview
        ctx: MainCtx
        source: Player.url
        duration: Player.length.toSeconds()
        position: Helpers.clamp(hoverHandler.point.position.x / control.width, 0, 1)
        available: Player.hasVideoOutput && Player.seekable && Player.length.valid()
        active: hoverHandler.hovered && available && !dragHandler.active
    }

    // A passive item keeps timeline clicks and video gestures out of the
    // popup overlay's event routing. It never grabs focus or pointer events.
    Rectangle {
        id: previewPopup
        parent: control
        z: 100
        visible: preview.active
        width: VLCStyle.dp(252, VLCStyle.scale)
        height: VLCStyle.dp(171, VLCStyle.scale)
        x: Math.max(0, Math.min(control.width - width,
                               hoverHandler.point.position.x - width / 2))
        y: -height - VLCStyle.dp(10, VLCStyle.scale)
        color: VLCStyle.redesign.surface
        border.width: VLCStyle.border
        border.color: VLCStyle.redesign.accent
        radius: VLCStyle.redesign.radius
        Column {
            anchors.fill: parent
            anchors.margins: VLCStyle.dp(6, VLCStyle.scale)
            spacing: VLCStyle.dp(4, VLCStyle.scale)
            Item {
                width: parent.width
                height: VLCStyle.dp(135, VLCStyle.scale)
                Image {
                    anchors.fill: parent
                    source: preview.image
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                Text {
                    anchors.centerIn: parent
                    visible: preview.image.toString() === ""
                    text: preview.busy ? qsTr("Loading preview…") : qsTr("Preview unavailable")
                    color: VLCStyle.redesign.foreground
                    font.pixelSize: VLCStyle.fontSize_normal
                }
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: Player.length.scale(preview.imagePosition).formatHMS(0)
                color: VLCStyle.redesign.foreground
                font.pixelSize: VLCStyle.fontSize_normal
            }
        }
    }

    FSM {
        id: fsm
        signal playerUpdatePosition(real position)
        signal pressControl(real position, bool forcePrecise)
        signal releaseControl(real position, bool forcePrecise)
        signal moveControl(real position, bool forcePrecise)
        signal inputChanged()

        //each signal is associated to a key, when a signal is received,
        //transitions of active state for the given key are evaluated
        signalMap: ({
            "playerUpdatePosition": fsm.playerUpdatePosition,
            "pressControl": fsm.pressControl,
            "releaseControl": fsm.releaseControl,
            "moveControl": fsm.moveControl,
            "inputChanged": fsm.inputChanged
        })

        initialState: fsmReleased

        function _setPositionFromValue(position) {
            control.value = position
        }

        function _seekToPosition(position, threshold, forcePrecise) {
            position = Helpers.clamp(position, 0., 1.)
            control.value = position
            if (!forcePrecise) {
                const chapter = Player.chapters.getClosestChapterFromPos(position, threshold)
                if (chapter !== -1) {
                    Player.chapters.selectChapter(chapter)
                    return
                }
            }
            Player.position = position
        }

        FSMState {
            id: fsmReleased

            transitions: ({
                "playerUpdatePosition": {
                    action: (position) => {
                        control.value = position
                    }
                },
                "pressControl": {
                    action: (position, forcePrecise) => {
                        control.forceActiveFocus()
                        fsm._seekToPosition(position, VLCStyle.dp(4, VLCStyle.scale) / control.width, forcePrecise)
                    },
                    target: fsmHeld
                }
            })
        }

        FSMState  {
            id: fsmHeld

            transitions: ({
                "moveControl": {
                    action: (position, forcePrecise) => {
                        fsm._seekToPosition(position, VLCStyle.dp(2, VLCStyle.scale) / control.width, forcePrecise)
                    }
                },
                "releaseControl": {
                    target: fsmReleased
                },
                "inputChanged": {
                    target: fsmHeldWrongInput
                }
            })
        }

        FSMState  {
            id: fsmHeldWrongInput

            function enter() {
                fsm._setPositionFromValue(Player.position)
            }

            transitions: ({
                "playerUpdatePosition": {
                    action: fsm._setPositionFromValue
                },
                "releaseControl": {
                    target: fsmReleased
                }
            })
        }
    }

    Connections {
        target: Player
        function onPositionChanged() {  fsm.playerUpdatePosition(Player.position) }
        function onInputChanged() {  fsm.inputChanged() }
    }

    Component.onCompleted: value = Player.position

    implicitHeight: control.barHeight
    height: implicitHeight

    padding: 0

    //we use our own HoverHandler
    hoverEnabled: false

    HoverHandler {
        id: hoverHandler
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor

        onHoveredChanged: () => {
            if (hovered) {
                if(Player.hasChapters)
                    control._isSeekPointsShown = true
            } else {
                if(Player.hasChapters)
                    seekpointTimer.restart()
            }
        }
    }

    contentItem: Item {
        implicitHeight: control.implicitHeight
        implicitWidth: control.implicitWidth

        //placing the TapHandler directly in the Control doesn't work with 6.2
        TapHandler {
            acceptedButtons: Qt.LeftButton

            gesturePolicy: TapHandler.WithinBounds

            // Make it easier to tap with touch screen:
            margin: MainCtx.usingTouch ? control.touchHandlerMargin : 0.0

            onPressedChanged: {
                if (pressed) {
                    fsm.pressControl(point.position.x / control.width, point.modifiers === Qt.ShiftModifier)
                } else {
                    // NOTE: Point is still valid at this point.
                    fsm.releaseControl(point.position.x / control.width, point.modifiers === Qt.ShiftModifier)
                }
            }
        }

        DragHandler {
            id: dragHandler
            acceptedButtons: Qt.LeftButton

            // Make it easier to drag with touch screen:
            margin: MainCtx.usingTouch ? control.touchHandlerMargin : 0

            target: null
            dragThreshold: 0
            grabPermissions: PointerHandler.CanTakeOverFromAnything

            property bool filterEvents: false

            function moveControl() {
                fsm.moveControl(dragHandler.centroid.position.x / control.width,
                                dragHandler.centroid.modifiers === Qt.ShiftModifier)
            }

            onActiveChanged: {
                if (active) {
                    filterEvents = false
                    fsm.pressControl(centroid.position.x / control.width, centroid.modifiers === Qt.ShiftModifier)
                } else {
                    fsm.releaseControl( centroid.position.x / control.width, centroid.modifiers === Qt.ShiftModifier)
                }
            }

            onCentroidChanged: {
                // FIXME: Qt 6.5 use xAxis.onActiveValueChanged in the DragHandler
                if (filterEvents) {
                    Qt.callLater(dragHandler.moveControl)
                } else {
                    dragHandler.moveControl()
                    filterEvents = true
                }
            }
        }

        WheelHandler {
            orientation: Qt.Vertical | Qt.Horizontal

            acceptedDevices: PointerDevice.AllDevices

            onWheel: (wheel) => {
                wheelToVLC.qmlWheelEvent(wheel)
                wheel.accepted = true
            }
        }

        WheelToVLCConverter {
            id: wheelToVLC

            function handleVertical(steps: int) {
                if (steps > 0)
                    Player.jumpFwd()
                else
                    Player.jumpBwd()
            }

            function handleHorizontal(steps: int) {
                if (steps > 0)
                    Player.jumpBwd()
                else
                    Player.jumpFwd()
            }

            Component.onCompleted: {
                wheelUpDown.connect(wheelToVLC, wheelToVLC.handleVertical)
                wheelLeftRight.connect(wheelToVLC, wheelToVLC.handleHorizontal)
            }
        }
    }

    background: Item {
        width: control.availableWidth
        implicitHeight: control.implicitHeight
        height: implicitHeight

        Rectangle {
            visible: control.rectangular
            width: control.availableWidth
            height: control.barHeight
            radius: VLCStyle.redesign.radius
            color: VLCStyle.redesign.track
            border.width: control.visualFocus ? VLCStyle.border : 0
            border.color: VLCStyle.redesign.accent

            Rectangle {
                x: VLCStyle.dp(2, VLCStyle.scale)
                y: x
                width: Math.max(0, (parent.width - 2 * x) * control.visualPosition)
                height: Math.max(0, parent.height - 2 * y)
                radius: VLCStyle.dp(4, VLCStyle.scale)
                color: VLCStyle.redesign.progress
            }
            // Chapter locations still come from VLC's chapter model. Do not
            // create the legacy segmented delegates for the rectangular skin.
            Repeater {
                model: control.rectangular && Player.hasChapters ? Player.chapters : null
                Rectangle {
                    required property var model
                    x: model.startPosition * (parent.width - width)
                    y: VLCStyle.dp(4, VLCStyle.scale)
                    width: VLCStyle.dp(1, VLCStyle.scale)
                    height: parent.height - 2 * y
                    visible: model.startPosition > 0
                    color: VLCStyle.redesign.foreground
                    opacity: 0.5
                }
            }
            Rectangle {
                width: Math.min(parent.width, VLCStyle.dp(8, VLCStyle.scale))
                height: Math.max(0, parent.height - VLCStyle.dp(4, VLCStyle.scale))
                x: Math.max(0, Math.min(parent.width - width - VLCStyle.dp(2, VLCStyle.scale),
                                       control.visualPosition * parent.width - width / 2))
                y: VLCStyle.dp(2, VLCStyle.scale)
                radius: VLCStyle.dp(4, VLCStyle.scale)
                color: VLCStyle.redesign.accent
            }
        }

        Rectangle {
            id: sliderRect
            visible: !control.rectangular && !Player.hasChapters
            color: control.backgroundColor
            anchors.fill: parent
            radius: implicitHeight
        }

        Repeater {
            id: seekpointsRptr

            anchors.left: parent.left
            anchors.right: parent.right
            height: control.barHeight
            visible: !control.rectangular && Player.hasChapters

            model: !control.rectangular && Player.hasChapters ? Player.chapters : null
            Item {
                Rectangle {
                    id: seekpointsRect
                    readonly property real startPosition: model.startPosition ??  0.0
                    readonly property real endPosition: model.endPosition ?? 1.0

                    readonly property int _currentChapter: {
                        if (control.visualPosition < seekpointsRect.startPosition)
                            return 1
                        else if (control.visualPosition > seekpointsRect.endPosition)
                            return -1
                        return 0
                    }
                    on_CurrentChapterChanged: {
                        if(_hovered)
                            control._currentChapterHovered = _currentChapter === 0
                    }

                    readonly property bool _hovered: hoverHandler.hovered &&
                                            (hoverHandler.point.position.x > x && hoverHandler.point.position.x < x+width)

                    color: _currentChapter < 0 ? theme.fg.primary : control.backgroundColor
                    width: sliderRect.width * seekpointsRect.endPosition - x - control._seekPointsDistance
                    x: sliderRect.width * seekpointsRect.startPosition

                    Rectangle {
                        id: progressRepRect
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        radius: parent.radius

                        width: sliderRect.width * control.visualPosition - parent.x - control._seekPointsDistance
                        visible: parent._currentChapter === 0
                        color: theme.fg.primary

                        // We use dynamic mutability group because time interpolation
                        // may induce rapid change:
                        // TODO: Do not use `Binding` when minimum Qt is 6.12:
                        Binding {
                            target: progressRepRect
                            property: "mutabilityGroup"
                            when: (progressRepRect.mutabilityGroup !== undefined)
                            value: (Player.playingState === Player.PLAYING_STATE_PLAYING) ? Item.DynamicMutabilityGroup
                                                                                          : Item.StaticMutabilityGroup
                        }
                    }
                }

                transitions: [
                    Transition {
                        to: "*"
                        SequentialAnimation{
                            PropertyAction { targets: control; property: "_currentChapterHovered" }
                            NumberAnimation {
                                targets: [seekpointsRect, progressRepRect]; properties: "height, y, radius"
                                duration: VLCStyle.duration_short; easing.type: Easing.InSine
                            }
                        }
                    }
                ]

                states:[
                    State {
                        name: "visible"
                        PropertyChanges {
                            target: control;
                            _currentChapterHovered: seekpointsRect._currentChapter === 0 ? false : control._currentChapterHovered
                        }
                        PropertyChanges { target: seekpointsRect; height: control.barHeight }
                        PropertyChanges { target: seekpointsRect; y: 0 }
                        PropertyChanges { target: seekpointsRect; radius: control._seekPointsRadius }
                    },
                    State {
                        name: "visibleLarge"
                        PropertyChanges {
                            target: control;
                            _currentChapterHovered: seekpointsRect._currentChapter === 0 ? true : control._currentChapterHovered
                        }
                        PropertyChanges { target: seekpointsRect; height: control._scaledBarHeight }
                        PropertyChanges { target: seekpointsRect; y: control._scaledY }
                        PropertyChanges { target: seekpointsRect; radius: control._scaledSeekPointsRadius }
                    }
                ]

                state: (seekpointsRect._hovered || (seekpointsRect._currentChapter === 0 && fsm._state === fsm.fsmHeld))
                       ? "visibleLarge"
                       : "visible"
            }
        }

        Rectangle {
            id: progressRect
            width: control.visualPosition * parent.width
            visible: !control.rectangular && !Player.hasChapters
            color: theme.fg.primary
            height: control.barHeight
            radius: control._seekPointsRadius

            // We use dynamic mutability group because time interpolation
            // may induce rapid change:
            // TODO: Do not use `Binding` when minimum Qt is 6.12:
            Binding {
                target: progressRect
                property: "mutabilityGroup"
                when: (progressRect.mutabilityGroup !== undefined)
                value: (Player.playingState === Player.PLAYING_STATE_PLAYING) ? Item.DynamicMutabilityGroup
                                                                              : Item.StaticMutabilityGroup
            }
        }

        Rectangle {
            id: bufferRect
            property int bufferAnimWidth: VLCStyle.dp(100, VLCStyle.scale)
            property int bufferAnimPosition: 0
            property int bufferFrames: 1000
            property alias animateLoading: loadingAnim.running
            property bool display: false
            property bool buffering: Player.buffering >= 0 && Player.buffering < 1

            // Keep buffering separate from the position fill in the compact
            // timeline, and debounce even the zero-buffer state during seeks.
            height: control.rectangular ? VLCStyle.dp(2, VLCStyle.scale) : control.barHeight
            y: control.rectangular ? control.barHeight - height : 0
            opacity: 0.4
            color: control.rectangular ? VLCStyle.redesign.accent : theme.fg.neutral
            radius: height

            // TODO: Do not use `Binding` when minimum Qt is 6.12:
            Binding {
                target: bufferRect
                property: "mutabilityGroup"
                when: bufferRect.mutabilityGroup !== undefined
                value: bufferRect.buffering ? Item.DynamicMutabilityGroup
                                            : Item.StaticMutabilityGroup
            }

            Timer {
                id: bufferingTimer
                interval: VLCStyle.duration_humanMoment
                repeat: false
                running: false
                onTriggered: {
                    bufferRect.display = true
                }
            }

            onBufferingChanged: {
                if(buffering)
                {
                    bufferingTimer.start()
                }
                else
                {
                    bufferingTimer.stop()
                    bufferRect.display = false
                }
            }

            states: [
                State {
                    name: "hidden"
                    when: !control.visible
                    PropertyChanges {
                        target: bufferRect
                        width: bufferAnimWidth
                        visible: false
                        x: 0
                        animateLoading: false
                    }
                },
                State {
                    name: "buffering not started"
                    when: control.visible && Player.buffering === 0
                          && (!control.rectangular || bufferRect.display)
                    PropertyChanges {
                        target: bufferRect
                        width: bufferAnimWidth
                        visible: true
                        x: (bufferAnimPosition / bufferFrames) * (parent.width - bufferAnimWidth)
                        animateLoading: true
                    }
                },
                State {
                    name: "time to start playing known"
                    when: control.visible && bufferRect.display
                    PropertyChanges {
                        target: bufferRect
                        width: Player.buffering * parent.width
                        visible: true
                        x: 0
                        animateLoading: false
                    }
                },
                State {
                    name: "playing from buffer"
                    when: control.visible && !bufferRect.display
                    PropertyChanges {
                        target: bufferRect
                        width: Player.buffering * parent.width
                        visible: false
                        x: 0
                        animateLoading: false
                    }
                }
            ]

            SequentialAnimation on bufferAnimPosition {
                id: loadingAnim
                running: bufferRect.animateLoading
                loops: Animation.Infinite
                PropertyAnimation {
                    from: 0.0
                    to: bufferRect.bufferFrames
                    duration: VLCStyle.durationSliderBouncing
                    easing.type: "OutBounce"
                }
                PauseAnimation {
                    duration: VLCStyle.duration_veryLong
                }
                PropertyAnimation {
                    from: bufferRect.bufferFrames
                    to: 0.0
                    duration: VLCStyle.durationSliderBouncing
                    easing.type: "OutBounce"
                }
                PauseAnimation {
                    duration: VLCStyle.duration_veryLong
                }
            }
        }
    }

    Rectangle {
        id: sliderHandle

        Binding on visible {
            when: control.rectangular
            value: false
        }

        property int _size: control.barHeight * 3

        x: (control.visualPosition * control.availableWidth) - width / 2
        y: (control.barHeight - height) / 2

        implicitWidth: sliderHandle._size
        implicitHeight: sliderHandle._size
        radius: VLCStyle.margin_small
        color: theme.fg.primary

        // We use dynamic mutability group because time interpolation
        // may induce rapid change:
        // TODO: Do not use `Binding` when minimum Qt is 6.12:
        Binding {
            target: sliderHandle
            property: "mutabilityGroup"
            when: (sliderHandle.mutabilityGroup !== undefined)
            value: (Player.playingState === Player.PLAYING_STATE_PLAYING) ? Item.DynamicMutabilityGroup
                                                                          : Item.StaticMutabilityGroup
        }

        transitions: [
            Transition {
                to: "hidden"
                SequentialAnimation {
                    NumberAnimation {
                        target: sliderHandle; properties: "implicitWidth,implicitHeight"
                        to: 0
                        duration: VLCStyle.duration_short; easing.type: Easing.OutSine
                    }
                    PropertyAction { target: sliderHandle; property: "visible"; value: false; }
                }
            },
            Transition {
                to: "visible"
                SequentialAnimation {
                    PropertyAction { target: sliderHandle; property: "visible"; value: true; }
                    NumberAnimation {
                        target: sliderHandle; properties: "implicitWidth,implicitHeight"
                        to: sliderHandle._size
                        duration: VLCStyle.duration_short; easing.type: Easing.InSine
                    }
                }
            },
            Transition {
                to: "visibleLarge"
                SequentialAnimation {
                    PropertyAction { target: sliderHandle; property: "visible"; value: true; }
                    NumberAnimation {
                        target: sliderHandle; properties: "implicitWidth,implicitHeight"
                        to: sliderHandle._size * (0.8 * control._hoveredScalingFactor)
                        duration: VLCStyle.duration_short; easing.type: Easing.InSine
                    }
                }
            }
        ]

        state: !control.rectangular && (hoverHandler.hovered || control.visualFocus)
               ? ((control._currentChapterHovered || (Player.hasChapters && fsm._state === fsm.fsmHeld)) ? "visibleLarge" : "visible")
               : "hidden"
    }
}
