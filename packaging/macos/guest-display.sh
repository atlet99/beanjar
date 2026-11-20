#!/bin/sh
# SPDX-FileCopyrightText: The Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# shortcut: display the open guest's boot screen until the webOS compositor is ported.
[ "$1" = start ] && [ -c /dev/fb0 ] || exit 0
fb=/sys/class/graphics/fb0
[ "$(cat "$fb/virtual_size")" = 800,600 ] &&
    [ "$(cat "$fb/stride")" = 3200 ] &&
    [ "$(cat "$fb/bits_per_pixel")" = 32 ] || exit 0
cat /usr/share/beanjar/boot-frame.bgra > /dev/fb0
