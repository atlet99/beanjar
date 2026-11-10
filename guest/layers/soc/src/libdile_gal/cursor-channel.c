// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The webos.cursor virtio-serial port (include/webos-cursor.h): the cursor plane as the host viewer
 * draws it. Its only writer is the display engine of libdile_gal, in gm
 * (gfx-memory.c): the plane's picture, its visibility and libim's cursor position
 * (DILE_GAL_MoveCursor, or SetCursorPosition on webOS 3).
 * A sender thread writes without blocking and repeats the state every 250 ms for a viewer that
 * connects late. */
#define _GNU_SOURCE
#include "cursor-channel.h"
#include "../../../../../include/webos-cursor.h"
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct packet {
    struct webos_cursor_header h;
    unsigned char pixels[WEBOS_CURSOR_MAX * WEBOS_CURSOR_MAX * 4];
};
static struct packet state; /* header filled by start(): a 256 KiB .bss, not .data */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static unsigned generation;
static int port(void)
{
    glob_t paths = {0};
    int fd = -1;
    if (!glob("/sys/class/virtio-ports/*/name", 0, NULL, &paths)) {
        for (size_t i = 0; i < paths.gl_pathc; i++) {
            char name[64];
            FILE *f = fopen(paths.gl_pathv[i], "r");
            if (!f)
                continue;
            int match = fgets(name, sizeof name, f) && !strcmp(name, "webos.cursor\n");
            fclose(f);
            if (match) {
                char device[128], leaf[64];
                if (sscanf(paths.gl_pathv[i], "/sys/class/virtio-ports/%63[^/]/name", leaf) == 1) {
                    snprintf(device, sizeof device, "/dev/%s", leaf);
                    fd = open(device, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
                }
                break;
            }
        }
    }
    globfree(&paths);
    return fd;
}
static void *sender(void *unused)
{
    (void)unused;
    struct packet pending;
    int fd = -1;
    unsigned last = 0, ticks = 25;
    size_t offset = 0, length = 0;
    for (;;) {
        if (fd < 0) {
            fd = port();
            if (fd < 0) {
                usleep(250000);
                continue;
            }
        }
        if (offset == length) {
            pthread_mutex_lock(&mutex);
            if (state.h.width && (generation != last || ticks >= 25)) {
                pending = state;
                last = generation;
                length = sizeof pending.h + (size_t)state.h.width * state.h.height * 4;
                offset = 0;
                ticks = 0;
                /* Wire is explicitly LE; all supported guests are LE ARM. */
                pending.h.bytes = htole32(length);
            }
            pthread_mutex_unlock(&mutex);
        }
        if (offset < length) {
            ssize_t n = write(fd, (char *)&pending + offset, length - offset);
            if (n > 0)
                offset += (size_t)n;
            else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                close(fd);
                fd = -1;
                offset = length = 0;
                ticks = 25;
                usleep(100000);
                continue;
            }
        }
        /* Never block the libim cursor thread on a missing/slow viewer. */
        usleep(10000);
        ticks++;
    }
    return NULL;
}
static void start(void)
{
    pthread_t thread;
    memcpy(state.h.magic, WEBOS_CURSOR_MAGIC, sizeof state.h.magic);
    state.h.screen_w = 1920;
    state.h.screen_h = 1080;
    if (!pthread_create(&thread, NULL, sender, NULL))
        pthread_detach(thread);
}
void cursor_channel_image(const void *bgra, unsigned w, unsigned h, unsigned pitch)
{
    if (!bgra || !w || !h || w > WEBOS_CURSOR_MAX || h > WEBOS_CURSOR_MAX || pitch < w * 4)
        return;
    pthread_once(&once, start);
    pthread_mutex_lock(&mutex);
    state.h.width = w;
    state.h.height = h;
    if (!state.h.draw_w) {
        state.h.draw_w = w;
        state.h.draw_h = h;
    }
    for (unsigned y = 0; y < h; y++)
        memcpy(state.pixels + y * w * 4, (const char *)bgra + y * pitch, w * 4);
    generation++;
    pthread_mutex_unlock(&mutex);
}
void cursor_channel_visible(int visible)
{
    pthread_once(&once, start);
    pthread_mutex_lock(&mutex);
    state.h.visible = !!visible;
    generation++;
    pthread_mutex_unlock(&mutex);
}
void cursor_channel_move(const void *data)
{
    /* WT1H DILE_GAL cursor position ABI, verified at libim's call site. */
    struct position {
        uint16_t x, y;
        uint32_t sw, sh, w, h, flags;
        uint16_t hx, hy;
    } p;
    if (!data)
        return;
    memcpy(&p, data, sizeof p);
    pthread_once(&once, start);
    pthread_mutex_lock(&mutex);
    state.h.x = p.x;
    state.h.y = p.y;
    state.h.screen_w = p.sw;
    state.h.screen_h = p.sh;
    state.h.draw_w = p.w;
    state.h.draw_h = p.h;
    state.h.hot_x = p.hx;
    state.h.hot_y = p.hy;
    generation++;
    pthread_mutex_unlock(&mutex);
}
