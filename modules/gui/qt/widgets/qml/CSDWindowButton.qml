/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
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

import VLC.Widgets as Widgets
import VLC.MainInterface
import VLC.Style


T.Button {
    id: control

    HoverHandler {
        enabled: control.enabled
        cursorShape: Qt.PointingHandCursor
    }

    property color color
    property color hoverColor
    property string iconTxt: ""
    property url imageSource
    property bool showHovered: false
    property bool isThemeDark: false
    property bool externalPressed: false

    readonly property bool _paintHovered: control.hovered || showHovered

    padding: 0
    width: VLCStyle.dp(40, VLCStyle.scale)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    focusPolicy: Qt.NoFocus

    background: Rectangle {
        height: control.height
        width: control.width
        color: {
            if (control.pressed || (control.externalPressed && control._paintHovered))
                return control.isThemeDark ? Qt.lighter(control.hoverColor, 1.2)
                                           : Qt.darker(control.hoverColor, 1.2)

            if (control._paintHovered)
                return control.hoverColor

            return "transparent"
        }
    }

    contentItem: Item {
        Image {
            anchors.centerIn: parent
            source: control.imageSource
            visible: source.toString().length > 0
            sourceSize: Qt.size(VLCStyle.dp(24, VLCStyle.scale), VLCStyle.dp(24, VLCStyle.scale))
        }
        Widgets.IconLabel {
            id: icon
            visible: !control.imageSource.toString().length
            anchors.centerIn: parent
            text: control.iconTxt

            font.family:{
                if (MainCtx.osName === MainCtx.Windows)
                {
                    if(MainCtx.osVersion === 10)
                        return "Segoe MDL2 Assets"

                    else if(MainCtx.osVersion >= 11)
                        return "Segoe Fluent Icons"
                }
     
                return VLCIcons.fontFamily
            }

            font.pixelSize: VLCStyle.icon_CSD
            color: control.color
        }
    }
}
