// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The viewer's cursor channel (host/viewer/core/cursor.c): packets, BGRA conversion, heartbeat
 * reuse, and when the host pointer should be transparent (over the TV image regardless of guest
 * cursor state). The GTK front end's use of it is checked by tests/integration/viewer/check-gtk.py.
 * Run under a private Xvfb server (SDL only provides the GL context for the texture upload). */
#define _DEFAULT_SOURCE
#include "../../../host/viewer/core/cursor.h"
#include <SDL.h>
#include <assert.h>
#include <endian.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static void send_frame(int fd, int visible)
{
    struct webos_cursor_header h = {.magic = WEBOS_CURSOR_MAGIC,
                                    .bytes = 56 + 8 * 12 * 4,
                                    .visible = visible,
                                    .screen_w = 1920,
                                    .screen_h = 1080,
                                    .width = 8,
                                    .height = 12,
                                    .draw_w = 8,
                                    .draw_h = 12,
                                    .hot_x = 2,
                                    .hot_y = 4};
    uint32_t fields[12];
    memcpy(fields, &h.bytes, sizeof fields);
    for (unsigned i = 0; i < 12; i++)
        fields[i] = htole32(fields[i]);
    memcpy(&h.bytes, fields, sizeof fields);
    unsigned char pixels[8 * 12 * 4];
    for (unsigned i = 0; i < sizeof pixels; i += 4) {
        pixels[i] = 20;
        pixels[i + 1] = 80;
        pixels[i + 2] = 220;
        pixels[i + 3] = 255;
    }
    assert(write(fd, &h, sizeof h) == sizeof h);
    assert(write(fd, pixels, sizeof pixels) == sizeof pixels);
}
int main(void)
{
    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
    assert(!SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_Window *window =
        SDL_CreateWindow("cursor test", 0, 0, 960, 540, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    assert(context);
    struct cursor c = {0};
    cursor_init(&c, "unused");
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair));
    c.fd = pair[0];
    send_frame(pair[1], 1);
    assert(cursor_poll(&c));
    assert(c.packets == 1 && c.images == 1 && c.rgba[0] == 220 && c.rgba[2] == 20);
    /* Pointer over the image: hidden independently of guest cursor state. */
    c.inside = 1;
    assert(cursor_blank(&c));
    /* Guest hides it: no host arrow replaces it; heartbeat reuses the texture. */
    send_frame(pair[1], 0);
    assert(cursor_poll(&c));
    assert(c.images == 1 && c.packets == 2 && cursor_blank(&c));
    send_frame(pair[1], 1);
    assert(cursor_poll(&c));
    assert(c.images == 1 && cursor_blank(&c));
    /* over the bars, the chrome or another window */
    c.inside = 0;
    assert(!cursor_blank(&c));
    /* Channel gone: the TV picture still owns pointer policy. */
    c.inside = 1;
    close(pair[1]);
    pair[1] = -1;
    cursor_poll(&c);
    assert(c.fd < 0 && cursor_blank(&c));
    c.h.width = 0;
    assert(cursor_blank(&c));
    c.inside = 0;
    assert(!cursor_blank(&c));
    cursor_close(&c);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("PASS: cursor packets, BGRA conversion, guest visibility, transparent pointer inside the "
         "image independent of guest state, disconnect, heartbeat reuse");
}
