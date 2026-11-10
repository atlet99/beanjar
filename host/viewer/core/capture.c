// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "capture.h"
#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* Every header word is LE32, including the two's complement errno. */
static void wire_header(struct webos_capture_header *h)
{
    uint32_t words[8];
    memcpy(words, h, sizeof(words));
    for (unsigned i = 0; i < 8; i++)
        words[i] = GUINT32_TO_LE(words[i]);
    memcpy(h, words, sizeof(words));
}
static void host_header(struct webos_capture_header *h)
{
    uint32_t words[8];
    memcpy(words, h, sizeof(words));
    for (unsigned i = 0; i < 8; i++)
        words[i] = GUINT32_FROM_LE(words[i]);
    memcpy(h, words, sizeof(words));
}

static void disconnect(struct capture *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
    c->used = c->length = c->sent = 0;
    c->next_hello = 0;
    free(c->response);
    c->response = NULL;
    c->retry = g_get_monotonic_time() + 200000;
}
void capture_init(struct capture *c, const char *path)
{
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    c->path = g_strdup(path);
}
static int flush(struct capture *c)
{
    size_t budget = 256 * 1024;
    while (c->sent < c->length && budget) {
        size_t n = MIN(c->length - c->sent, budget);
        ssize_t k = send(c->fd, c->response + c->sent, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR)
            continue;
        if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        if (k <= 0) {
            disconnect(c);
            return 0;
        }
        c->sent += k;
        budget -= k;
    }
    if (c->sent < c->length)
        return 0;
    free(c->response);
    c->response = NULL;
    c->length = c->sent = 0;
    return 1;
}
/* QEMU can accept the viewer socket before the guest frontend opens its
 * virtio port, dropping the first hello. Repeat availability while idle. */
static int announce(struct capture *c)
{
    struct webos_capture_header hello = {.magic = WEBOS_CAPTURE_MAGIC};
    c->response = malloc(sizeof(hello));
    if (!c->response) {
        disconnect(c);
        return 0;
    }
    wire_header(&hello);
    memcpy(c->response, &hello, sizeof(hello));
    c->length = sizeof(hello);
    c->next_hello = g_get_monotonic_time() + 1000000;
    return 1;
}
void capture_poll(struct capture *c, capture_render render, void *data)
{
    if (c->fd < 0) {
        if (g_get_monotonic_time() < c->retry || !c->path)
            return;
        struct sockaddr_un addr = {.sun_family = AF_UNIX};
        if (strlen(c->path) >= sizeof(addr.sun_path))
            return;
        strcpy(addr.sun_path, c->path);
        c->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (c->fd < 0 || connect(c->fd, (void *)&addr, sizeof(addr)) < 0) {
            disconnect(c);
            return;
        }
        if (!announce(c))
            return;
    }
    if (c->response && !flush(c))
        return;
    ssize_t n = recv(c->fd, c->request + c->used, sizeof(c->request) - c->used, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!c->used && g_get_monotonic_time() >= c->next_hello && announce(c))
            flush(c);
        return;
    }
    if (n < 0 && errno == EINTR)
        return;
    if (n <= 0) {
        disconnect(c);
        return;
    }
    c->used += n;
    if (c->used != sizeof(c->request))
        return;
    struct webos_capture_header h;
    memcpy(&h, c->request, sizeof(h));
    host_header(&h);
    c->used = 0;
    if (h.magic != WEBOS_CAPTURE_MAGIC || !h.sequence || h.size || h.status || h.reserved ||
        !h.width || !h.height || h.width > WEBOS_CAPTURE_WIDTH || h.height > WEBOS_CAPTURE_HEIGHT) {
        disconnect(c);
        return;
    }
    unsigned char *pixels = NULL;
    h.status = render(data, &h, &pixels);
    h.size = h.status ? 0 : h.width * h.height * 4;
    c->response = malloc(sizeof(h) + h.size);
    if (!c->response) {
        free(pixels);
        disconnect(c);
        return;
    }
    struct webos_capture_header encoded = h;
    wire_header(&encoded);
    memcpy(c->response, &encoded, sizeof(encoded));
    if (h.size)
        memcpy(c->response + sizeof(h), pixels, h.size);
    free(pixels);
    c->length = sizeof(h) + h.size;
    flush(c);
}
void capture_close(struct capture *c)
{
    disconnect(c);
    g_free(c->path);
    c->path = NULL;
}
