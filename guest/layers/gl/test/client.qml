// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick 2.0
import QtQuick.Window 2.0
Window {
    width: 1920; height: 1080; visible: true; color: "#2040a0"
    Rectangle { x: 660; y: 340; width: 600; height: 400; color: "red"; rotation: 15 }
    Text { x: 80; y: 80; text: "Wayland client on Mesa, composited by surface-manager"; color: "white"; font.pixelSize: 48 }
}
