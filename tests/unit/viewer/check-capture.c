// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Standalone socket regression; link with host/viewer/core/capture.c and glib-2.0. */
#define _POSIX_C_SOURCE 200809L
#include "../../../host/viewer/core/capture.h"
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static unsigned renders;
static int render(void *data, const struct webos_capture_header *h, unsigned char **pixels)
{
    (void)data;
    ++renders;
    size_t size = h->width * h->height * 4;
    *pixels = malloc(size);
    assert(*pixels);
    for (size_t i = 0; i < size; ++i)
        (*pixels)[i] = (unsigned char)(i * 37);
    return 0;
}
static void header(int fd)
{
    struct webos_capture_header h;
    assert(recv(fd, &h, sizeof h, MSG_WAITALL) == sizeof h);
    assert(GUINT32_FROM_LE(h.magic) == WEBOS_CAPTURE_MAGIC);
    assert(!h.sequence && !h.mode && !h.width && !h.height && !h.size && !h.status && !h.reserved);
}
int main(void)
{
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    snprintf(addr.sun_path, sizeof addr.sun_path, "capture-protocol-%ld.sock", (long)getpid());
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(listener >= 0);
    assert(!bind(listener, (void *)&addr, sizeof addr));
    assert(!listen(listener, 1));
    struct capture c;
    capture_init(&c, addr.sun_path);
    capture_poll(&c, render, NULL);
    int peer = accept(listener, NULL, NULL);
    assert(peer >= 0);
    /* Discard the early announcement, as QEMU can before frontend-open. */
    header(peer);
    assert(!renders && !c.response);
    g_usleep(1100000);
    capture_poll(&c, render, NULL);
    header(peer);
    puts("PASS initial and later idle provider hello");
    struct webos_capture_header request = {.magic = GUINT32_TO_LE(WEBOS_CAPTURE_MAGIC),
                                           .sequence = GUINT32_TO_LE(17),
                                           .mode = GUINT32_TO_LE(WEBOS_CAPTURE_BLENDED),
                                           .width = GUINT32_TO_LE(1920),
                                           .height = GUINT32_TO_LE(1080)};
    assert(send(peer, &request, 11, 0) == 11);
    capture_poll(&c, render, NULL);
    assert(c.used == 11 && !renders);
    c.next_hello = 0;
    capture_poll(&c, render, NULL);
    unsigned char byte;
    assert(recv(peer, &byte, 1, MSG_DONTWAIT) < 0 && errno == EAGAIN);
    assert(send(peer, (char *)&request + 11, sizeof request - 11, 0) == sizeof request - 11);
    capture_poll(&c, render, NULL);
    assert(renders == 1 && c.response && c.sent < c.length);
    size_t total = sizeof request + WEBOS_CAPTURE_MAX, used = 0;
    unsigned char *reply = malloc(total);
    assert(reply);
    gint64 deadline = g_get_monotonic_time() + 10000000;
    while (used < total) {
        assert(g_get_monotonic_time() < deadline);
        ssize_t n = recv(peer, reply + used, total - used, MSG_DONTWAIT);
        if (n > 0)
            used += n;
        else
            assert(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        capture_poll(&c, render, NULL);
    }
    struct webos_capture_header h;
    memcpy(&h, reply, sizeof h);
    assert(GUINT32_FROM_LE(h.sequence) == 17 && GUINT32_FROM_LE(h.size) == WEBOS_CAPTURE_MAX &&
           !h.status);
    for (size_t i = 0; i < WEBOS_CAPTURE_MAX; ++i)
        assert(reply[sizeof h + i] == (unsigned char)(i * 37));
    free(reply);
    assert(renders == 1);
    puts("PASS fragmented request and bounded full-frame reply without hello interleaving");
    /* Begin another response, then disconnect with pixels still queued. */
    assert(send(peer, &request, sizeof request, 0) == sizeof request);
    capture_poll(&c, render, NULL);
    assert(c.response && renders == 2);
    close(peer);
    capture_poll(&c, render, NULL);
    assert(c.fd == -1 && !c.response && !c.used && !c.length && !c.sent && !c.next_hello);
    capture_close(&c);
    assert(!c.path);
    close(listener);
    assert(!unlink(addr.sun_path));
    puts("PASS disconnect clears queued response, partial state and announcement timer");
}
