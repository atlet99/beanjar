// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Socket-pair regression for QtWayland 5.0 + modern Wayland event queues.
 * A tiny wire peer sends callback.done events without display.delete_id (which
 * would mask the zero-dispatch case by adding a display-queue event).
 * Run against either the native library or the generated ARM GL layer.
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

struct fixture {
    struct wl_display *display;
    struct wl_event_queue *queue;
    struct wl_callback *target, *other;
    int peer, target_done, other_done;
};

static void done(void *data, struct wl_callback *cb, uint32_t serial)
{
    (void)cb;
    assert(serial == 42);
    ++*(int *)data;
}
static const struct wl_callback_listener listener = {done};

static void send_done(int fd, uint32_t id)
{
    uint32_t event[] = {id, 12u << 16, 42};
    assert(write(fd, event, sizeof event) == sizeof event);
}

static void read_exact(int fd, void *data, size_t size)
{
    char *p = data;
    while (size) {
        ssize_t n = read(fd, p, size);
        if (n < 0 && errno == EINTR)
            continue;
        assert(n > 0);
        p += n;
        size -= n;
    }
}

static void init(struct fixture *f)
{
    int pair[2];
    *f = (struct fixture){0};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    f->peer = pair[1];
    f->display = wl_display_connect_to_fd(pair[0]);
    assert(f->display);
    f->queue = wl_display_create_queue(f->display);
    assert(f->queue);
    f->target = wl_display_sync(f->display);
    f->other = wl_display_sync(f->display);
    assert(f->target && f->other);
    wl_proxy_set_queue((struct wl_proxy *)f->other, f->queue);
    assert(wl_callback_add_listener(f->target, &listener, &f->target_done) == 0);
    assert(wl_callback_add_listener(f->other, &listener, &f->other_done) == 0);
    assert(wl_display_flush(f->display) >= 0);
    uint32_t requests[6];
    read_exact(f->peer, requests, sizeof requests);
}

static void finish(struct fixture *f)
{
    wl_callback_destroy(f->target);
    wl_callback_destroy(f->other);
    wl_event_queue_destroy(f->queue);
    wl_display_disconnect(f->display);
    close(f->peer);
}

static uint32_t id(struct wl_callback *cb)
{
    return wl_proxy_get_id((struct wl_proxy *)cb);
}

/* Supply the event later: an initial read must still block, and the timeout
 * APIs must keep waiting after they read an event for a different queue. */
struct delayed {
    int fd;
    uint32_t id;
};
static void *later(void *arg)
{
    struct delayed *d = arg;
    struct timespec delay = {.tv_nsec = 80000000};
    while (nanosleep(&delay, &delay) < 0)
        assert(errno == EINTR);
    send_done(d->fd, d->id);
    return NULL;
}

static void other_queue_only(int custom)
{
    struct fixture f;
    init(&f);
    send_done(f.peer, id(custom ? f.target : f.other));
    int n = custom ? wl_display_dispatch_queue(f.display, f.queue) : wl_display_dispatch(f.display);
    assert(n == 0);
    assert(f.target_done == 0 && f.other_done == 0);
    /* The socket is now empty. Dispatching the pending event must not read. */
    n = custom ? wl_display_dispatch(f.display) : wl_display_dispatch_queue(f.display, f.queue);
    assert(n == 1);
    assert(custom ? f.target_done == 1 : f.other_done == 1);
    finish(&f);
}

static void blocking_and_timeout(int mode)
{
    struct fixture f;
    init(&f);
    if (mode >= 3) {
        wl_proxy_set_queue((struct wl_proxy *)f.target, f.queue);
        wl_proxy_set_queue((struct wl_proxy *)f.other, NULL);
    }
    struct delayed d = {f.peer, id(f.target)};
    pthread_t t;
    if (mode)
        send_done(f.peer, id(f.other));
    assert(pthread_create(&t, NULL, later, &d) == 0);
    struct timespec timeout = {.tv_sec = 1};
    int n;
    if (mode == 0)
        n = wl_display_dispatch(f.display);
    else if (mode == 1)
        n = wl_display_dispatch_timeout(f.display, &timeout);
    else if (mode == 2)
        n = wl_display_dispatch_timeout(f.display, NULL);
    else
        n = wl_display_dispatch_queue_timeout(f.display, f.queue, mode == 3 ? &timeout : NULL);
    assert(n == 1 && f.target_done == 1 && f.other_done == 0);
    assert(pthread_join(t, NULL) == 0);
    if (mode) {
        n = mode >= 3 ? wl_display_dispatch_pending(f.display)
                      : wl_display_dispatch_queue_pending(f.display, f.queue);
        assert(n == 1);
    }
    finish(&f);
}

static void timeouts(void)
{
    struct fixture f;
    init(&f);
    struct timespec timeout = {.tv_nsec = 40000000}, zero = {0};
    send_done(f.peer, id(f.other));
    assert(wl_display_dispatch_timeout(f.display, &timeout) == 0);
    assert(f.target_done == 0 && f.other_done == 0);
    assert(wl_display_dispatch_queue_pending(f.display, f.queue) == 1);
    assert(wl_display_dispatch_timeout(f.display, &zero) == 0);
    /* A timeout must cancel read preparation, so another read still works. */
    send_done(f.peer, id(f.target));
    assert(wl_display_dispatch(f.display) == 1 && f.target_done == 1);
    finish(&f);
}

static void *roundtrip_peer(void *arg)
{
    struct fixture *f = arg;
    uint32_t request[3];
    read_exact(f->peer, request, sizeof request);
    assert(request[0] == 1 && request[1] == (12u << 16));
    struct delayed d = {f->peer, request[2]};
    return later(&d);
}

static void roundtrip(int custom)
{
    struct fixture f;
    init(&f);
    /* Put the first event outside the queue used for the sync callback. */
    if (custom)
        wl_proxy_set_queue((struct wl_proxy *)f.other, NULL);
    send_done(f.peer, id(f.other));
    pthread_t t;
    assert(pthread_create(&t, NULL, roundtrip_peer, &f) == 0);
    int n =
        custom ? wl_display_roundtrip_queue(f.display, f.queue) : wl_display_roundtrip(f.display);
    assert(n >= 1);
    assert(pthread_join(t, NULL) == 0);
    n = custom ? wl_display_dispatch_pending(f.display)
               : wl_display_dispatch_queue_pending(f.display, f.queue);
    assert(n == 1 && f.other_done == 1);
    finish(&f);
}

int main(void)
{
    /* A regression must fail rather than hang the test runner indefinitely. */
    alarm(5);
    other_queue_only(0);
    puts("PASS default dispatch returns zero for another queue's event");
    other_queue_only(1);
    puts("PASS custom dispatch returns zero; pending default events still dispatch");
    for (int mode = 0; mode < 5; ++mode)
        blocking_and_timeout(mode);
    puts("PASS initial read blocks; finite/NULL timeout APIs wait for their event");
    timeouts();
    puts("PASS finite/zero timeout and read cancellation");
    roundtrip(0);
    roundtrip(1);
    puts("PASS default/custom roundtrips after unrelated events");
    alarm(0);
    return 0;
}
