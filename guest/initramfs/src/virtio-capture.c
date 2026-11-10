// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Fresh, bounded capture replies between the viewer and lgemu. No frame cache. */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../../../include/webos-capture.h"

static volatile sig_atomic_t stopping;
static void stop(int sig)
{
    (void)sig;
    stopping = 1;
}
static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void pause_retry(void)
{
    struct timespec t = {0, 200000000};
    while (!stopping && nanosleep(&t, &t) < 0 && errno == EINTR) {
    }
}
static void find_port(char *dev, size_t size)
{
    glob_t paths = {0};
    dev[0] = 0;
    if (glob("/sys/class/virtio-ports/*/name", 0, NULL, &paths))
        return;
    for (size_t i = 0; i < paths.gl_pathc && !dev[0]; ++i) {
        char line[64], leaf[64];
        FILE *f = fopen(paths.gl_pathv[i], "r");
        if (!f)
            continue;
        if (fgets(line, sizeof line, f) && !strcmp(line, "webos.capture\n") &&
            sscanf(paths.gl_pathv[i], "/sys/class/virtio-ports/%63[^/]/name", leaf) == 1)
            snprintf(dev, size, "/dev/%s", leaf);
        fclose(f);
    }
    globfree(&paths);
}
/* The misc device takes one complete packet; never split a write to it. */
static int deliver(int fd, const void *packet, size_t size)
{
    ssize_t n;
    do {
        n = write(fd, packet, size);
    } while (n < 0 && errno == EINTR && !stopping);
    return n == (ssize_t)size || (n < 0 && errno == ESTALE) ? 0 : -1;
}
static int valid(const struct webos_capture_header *h)
{
    if (h->magic != WEBOS_CAPTURE_MAGIC || h->reserved || h->size > WEBOS_CAPTURE_MAX ||
        h->status > 0 || h->status < -4095)
        return 0;
    if (!h->sequence)
        return !h->size && !h->mode && !h->width && !h->height &&
               (!h->status || h->status == -ENODEV);
    return h->mode >= WEBOS_CAPTURE_VIDEO && h->mode <= WEBOS_CAPTURE_OSD && h->width &&
           h->width <= WEBOS_CAPTURE_WIDTH && h->height && h->height <= WEBOS_CAPTURE_HEIGHT &&
           h->size == (h->status ? 0 : h->width * h->height * 4);
}
/* Return on transport loss, protocol errors or a stalled transaction. Reopen
 * the transport with no buffered bytes, and wait for a new provider hello. */
static void connected(int port, int capture, unsigned char *packet)
{
    struct webos_capture_header tx = {0};
    size_t sent = sizeof tx, used = 0, needed = sizeof tx;
    int ready = 0;
    int64_t deadline = 0;
    for (; !stopping;) {
        struct pollfd p[2] = {{port, POLLIN | (sent < sizeof tx ? POLLOUT : 0), 0},
                              {ready && sent == sizeof tx && !deadline ? capture : -1, POLLIN, 0}};
        int timeout = -1;
        if (deadline) {
            int64_t remaining = deadline - now_ms();
            if (remaining <= 0)
                return;
            timeout = (int)remaining;
        }
        int rc = poll(p, 2, timeout);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        if (!rc || p[0].revents & (POLLHUP | POLLERR | POLLNVAL) ||
            p[1].revents & (POLLHUP | POLLERR | POLLNVAL))
            return;
        if (p[0].revents & POLLIN) {
            ssize_t n = read(port, packet + used, needed - used);
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            if (n <= 0)
                return;
            if (!deadline)
                deadline = now_ms() + 3000;
            used += n;
            if (used == sizeof tx && needed == sizeof tx) {
                struct webos_capture_header h;
                memcpy(&h, packet, sizeof h);
                if (!valid(&h) || (!ready && h.sequence))
                    return;
                needed += h.size;
            }
            if (used == needed) {
                struct webos_capture_header h;
                memcpy(&h, packet, sizeof h);
                /* Only the currently sent request can satisfy the kernel. */
                if (!h.sequence ||
                    (sent == sizeof tx && h.sequence == tx.sequence && h.mode == tx.mode &&
                     h.width == tx.width && h.height == tx.height)) {
                    if (deliver(capture, packet, needed))
                        return;
                    if (!h.sequence)
                        ready = !h.status;
                    if (!h.sequence && h.status)
                        return;
                    if (h.sequence) {
                        tx.sequence = 0;
                        deadline = 0;
                    } else if (!tx.sequence)
                        deadline = 0;
                } else if (!tx.sequence)
                    deadline = 0;
                used = 0;
                needed = sizeof tx;
            }
        }
        if (p[0].revents & POLLOUT) {
            ssize_t n = write(port, (unsigned char *)&tx + sent, sizeof tx - sent);
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            if (n <= 0)
                return;
            sent += n;
        }
        if (p[1].revents & POLLIN) {
            ssize_t n = read(capture, &tx, sizeof tx);
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            if (n != sizeof tx)
                return;
            sent = 0;
            deadline = now_ms() + 3000;
        }
    }
}
int main(void)
{
    char dev[96];
    find_port(dev, sizeof dev);
    if (!dev[0] || access("/dev/lgemu-capture", F_OK))
        return 0;
    int capture = open("/dev/lgemu-capture", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (capture < 0) {
        perror("virtio-capture: capture");
        return 1;
    }
    unsigned char *packet = malloc(sizeof(struct webos_capture_header) + WEBOS_CAPTURE_MAX);
    if (!packet) {
        close(capture);
        return 1;
    }
    struct sigaction sa = {.sa_handler = stop};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    struct webos_capture_header gone = {.magic = WEBOS_CAPTURE_MAGIC, .status = -ENODEV};
    while (!stopping) {
        int port = open(dev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (port < 0 && (errno == ENOENT || errno == ENODEV))
            break;
        if (port >= 0) {
            connected(port, capture, packet);
            close(port);
        }
        if (deliver(capture, &gone, sizeof gone))
            break;
        pause_retry();
    }
    close(capture);
    free(packet);
    return 0;
}
