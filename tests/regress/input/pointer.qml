// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick 2.0
Rectangle {
    width: 1920; height: 1080
    color: "#18304a"
    focus: true
    Keys.onPressed: console.warn("POINTER_KEY_DOWN " + event.key)
    Keys.onReleased: console.warn("POINTER_KEY_UP " + event.key)
    property int clicks: 0
    property int wheels: 0
    property int wheelEvents: 0
    property string position: "waiting"
    Text {
        anchors.centerIn: parent
        text: "Pointer " + position + "\nClicks " + clicks + "\nWheel events " + wheelEvents + " total " + wheels
        color: "white"; font.pixelSize: 48
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPositionChanged: {
            position = Math.round(mouse.x) + "," + Math.round(mouse.y)
            console.warn("POINTER_MOVE " + position)
        }
        onClicked: {
            clicks++
            console.warn("POINTER_CLICK " + Math.round(mouse.x) + "," + Math.round(mouse.y) + " button=" + mouse.button)
        }
        onWheel: {
            wheelEvents++
            wheels += wheel.angleDelta.y
            console.warn("POINTER_WHEEL " + wheel.angleDelta.y)
        }
    }
}
