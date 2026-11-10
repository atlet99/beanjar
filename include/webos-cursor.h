// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef WEBOS_CURSOR_H
#define WEBOS_CURSOR_H
#include <stdint.h>
/* Little-endian, self-contained frames on the webos.cursor virtio-serial port.
 * Header followed by width*height*4 BGRA bytes (straight alpha, top row first).
 * Coordinates/hotspot and draw size come from libim's DILE_GAL_MoveCursor call.
 * Every frame includes the bitmap so a newly connected viewer can synchronize. */
#define WEBOS_CURSOR_MAGIC "WOSCUR1"
#define WEBOS_CURSOR_MAX   256
struct webos_cursor_header {
    char magic[8];
    uint32_t bytes, visible;
    int32_t x, y;
    uint32_t screen_w, screen_h, width, height;
    uint32_t draw_w, draw_h, hot_x, hot_y;
};
_Static_assert(sizeof(struct webos_cursor_header) == 56, "cursor wire header");
#endif
