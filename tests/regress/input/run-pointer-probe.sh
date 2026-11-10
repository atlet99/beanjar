#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#   (the VM gets it with `host/scripts/vm-launcher put tests/regress/input/run-pointer-probe.sh` on the host)
# Run inside webOS (webos-enter sh /mnt/host/tests/regress/input/run-pointer-probe.sh).
export XDG_RUNTIME_DIR=/tmp/xdg QT_QPA_PLATFORM=wayland
export QT_WAYLAND_HARDWARE_INTEGRATION=wayland-egl QT_VER=qt5
export QML2_IMPORT_PATH=/usr/lib/qt5/qml QT_QPA_FONTDIR=/usr/share/fonts
export STARFISH_SCREEN_WIDTH=1920 STARFISH_SCREEN_HEIGHT=1080
exec /usr/bin/qml-runner '{"main":"file:///mnt/host/tests/regress/input/pointer.qml","appId":"com.webos.app.pointertest"}'
