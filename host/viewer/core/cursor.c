// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _DEFAULT_SOURCE
#include "cursor.h"
#include <endian.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
void cursor_init(struct cursor *c, const char *path)
{
    c->fd = -1;
    c->path = path ? strdup(path) : NULL;
    glGenTextures(1, &c->texture);
    glBindTexture(GL_TEXTURE_2D, c->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
static void disconnect(struct cursor *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
    c->used = 0;
    c->h.visible = 0;
    c->h.width = 0;
    c->h.height = 0;
}
int cursor_poll(struct cursor *c)
{
    if (c->fd < 0) {
        struct sockaddr_un addr = {.sun_family = AF_UNIX};
        if (strlen(c->path) >= sizeof addr.sun_path)
            return 0;
        strcpy(addr.sun_path, c->path);
        c->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (c->fd < 0)
            return 0;
        if (connect(c->fd, (void *)&addr, sizeof addr) < 0) {
            disconnect(c);
            return 0;
        }
    }
    int changed = 0;
    /* Bound work per display iteration even if a guest sends continuously. */
    for (unsigned count = 0; count < 16; count++) {
        ssize_t n = read(c->fd, c->buffer + c->used, sizeof c->buffer - c->used);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            disconnect(c);
            return 1;
        }
        if (n < 0)
            break;
        c->used += (size_t)n;
        while (c->used >= sizeof c->h) {
            struct webos_cursor_header h;
            memcpy(&h, c->buffer, sizeof h);
            uint32_t fields[12];
            memcpy(fields, &h.bytes, sizeof fields);
            for (unsigned i = 0; i < 12; i++)
                fields[i] = le32toh(fields[i]);
            memcpy(&h.bytes, fields, sizeof fields);
            if (memcmp(h.magic, WEBOS_CURSOR_MAGIC, 8) || !h.width || !h.height ||
                h.width > WEBOS_CURSOR_MAX || h.height > WEBOS_CURSOR_MAX ||
                h.bytes != sizeof h + h.width * h.height * 4 || h.visible > 1 || !h.screen_w ||
                !h.screen_h || h.screen_w > 8192 || h.screen_h > 8192 || h.draw_w > 8192 ||
                h.draw_h > 8192 || h.hot_x > WEBOS_CURSOR_MAX || h.hot_y > WEBOS_CURSOR_MAX) {
                fprintf(stderr, "invalid cursor packet\n");
                disconnect(c);
                return 1;
            }
            if (c->used < h.bytes)
                break;
            unsigned char *pixels = c->buffer + sizeof h;
            for (unsigned i = 0; i < h.width * h.height; i++) {
                unsigned char b = pixels[i * 4];
                pixels[i * 4] = pixels[i * 4 + 2];
                pixels[i * 4 + 2] = b;
            }
            if (h.width != c->h.width || h.height != c->h.height ||
                memcmp(c->rgba, pixels, h.width * h.height * 4)) {
                memcpy(c->rgba, pixels, h.width * h.height * 4);
                c->revision++;
                c->images++;
                glBindTexture(GL_TEXTURE_2D, c->texture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, h.width, h.height, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, pixels);
            }
            if (h.visible != c->h.visible) {
                if (h.visible)
                    c->shown++;
                else
                    c->hidden++;
            }
            if (!c->packets)
                fprintf(
                    stderr,
                    "guest cursor: %ux%u draw=%ux%u screen=%ux%u hotspot=%u,%u position=%d,%d\n",
                    h.width, h.height, h.draw_w, h.draw_h, h.screen_w, h.screen_h, h.hot_x, h.hot_y,
                    h.x, h.y);
            c->h = h;
            c->packets++;
            changed = 1;
            c->used -= h.bytes;
            memmove(c->buffer, c->buffer + h.bytes, c->used);
        }
    }
    return changed;
}
/* The guest's picture is drawn by the viewer (core.c, draw()), LG's cursor included. */
int cursor_blank(const struct cursor *c)
{
    return c->inside;
}
void cursor_close(struct cursor *c)
{
    fprintf(stderr, "cursor_packets=%u cursor_shown=%u cursor_hidden=%u\n", c->packets, c->shown,
            c->hidden);
    fprintf(stderr, "cursor_images=%u cursor_blank_sets=%u position=%d,%d hotspot=%u,%u\n",
            c->images, c->blank_sets, c->h.x, c->h.y, c->h.hot_x, c->h.hot_y);
    disconnect(c);
    free(c->path);
    glDeleteTextures(1, &c->texture);
}
