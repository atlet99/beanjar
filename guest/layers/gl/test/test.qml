// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick 2.0
import QtQuick.Window 2.0
Window {
    width: 160; height: 120; visible: true; color: "blue"
    Rectangle { x: 40; y: 30; width: 80; height: 60; color: "red"; rotation: 20 }
    Text { x: 4; y: 4; text: "Mesa"; color: "white"; font.pixelSize: 16 }
}
