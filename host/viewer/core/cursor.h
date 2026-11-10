// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_CURSOR_H
#define VIEWER_CURSOR_H
#include "../../../include/webos-cursor.h"
#include <GLES2/gl2.h>
#include <stddef.h>
struct cursor {
    int fd;
    char *path; /* webos.cursor transport socket */
    size_t used;
    unsigned char
        buffer[sizeof(struct webos_cursor_header) + WEBOS_CURSOR_MAX * WEBOS_CURSOR_MAX * 4];
    struct webos_cursor_header h;
    GLuint texture;
    unsigned packets, shown, hidden, images, revision;
    unsigned char rgba[WEBOS_CURSOR_MAX * WEBOS_CURSOR_MAX * 4];
    /* the host pointer is over the TV image (the front end then makes it transparent while
     * cursor_blank() says so) */
    int inside;
    unsigned blank_sets; /* times the front end made it transparent */
    /* the host pointer, logical window pixels, while inside the TV image */
    int local_x, local_y;
};
void cursor_init(struct cursor *c, const char *path);
int cursor_poll(struct cursor *c);
/* 1 while the pointer is inside the TV picture, independent of guest cursor
 * shape, visibility or transport connection. */
int cursor_blank(const struct cursor *c);
void cursor_close(struct cursor *c);
#endif
