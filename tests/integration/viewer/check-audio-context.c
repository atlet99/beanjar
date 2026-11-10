// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Guest Writes must progress without servicing the display's default main context.
 * The Python fixture owns fake-qemu and the private bus; this program creates no window. */
#define _POSIX_C_SOURCE 200809L
#include "../../../host/viewer/core/audio.h"
#include "../../../host/viewer/core/peer.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

struct received {
    GThread *caller;
    unsigned calls, frames;
    int wrong_thread;
};
static void samples(void *data, const struct audio_out *a, const unsigned char *pcm, size_t len)
{
    struct received *r = data;
    if (!pcm)
        return;
    r->calls++;
    r->frames += len / a->frame;
    r->wrong_thread |= g_thread_self() == r->caller;
}
struct stalled {
    int fd;
    GCancellable *cancel;
    GError *error;
    GDBusConnection *conn;
};
static gpointer authenticate(gpointer data)
{
    struct stalled *s = data;
    s->conn = peer_connect_cancellable(s->fd, s->cancel, &s->error);
    return NULL;
}
int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3)
        return 2;
    GError *error = NULL;
    GDBusConnection *bus =
        g_dbus_connection_new_for_address_sync(argv[1],
                                               G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                   G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                               NULL, NULL, &error);
    if (!bus) {
        fprintf(stderr, "FAIL bus: %s\n", error->message);
        return 1;
    }
    struct audio_out audio;
    if (argc == 3) {
        audio_connect(&audio, bus, 0, NULL, NULL, NULL);
        gint64 deadline = g_get_monotonic_time() + 2000000;
        while (!g_file_test(argv[2], G_FILE_TEST_EXISTS) && g_get_monotonic_time() < deadline)
            g_usleep(1000);
        if (!g_file_test(argv[2], G_FILE_TEST_EXISTS)) {
            fprintf(stderr, "FAIL fake never received pending registration\n");
            audio_close(&audio);
            return 1;
        }
        gint64 start = g_get_monotonic_time();
        audio_close(&audio);
        gint64 elapsed = g_get_monotonic_time() - start;
        printf("Pending RegisterOutListener cancelled: %.2f ms\n", elapsed / 1000.0);
        if (elapsed > 500000) {
            fprintf(stderr, "FAIL cancellation waited for unresponsive QEMU\n");
            return 1;
        }
        g_object_unref(bus);
        puts("PASS pending registration teardown");
        return 0;
    }
    struct received r = {.caller = g_thread_self()};
    audio_connect(&audio, bus, 0, NULL, samples, &r);
    /* Deliberately no g_main_context_iteration here: the old listener receives zero Writes. */
    g_usleep(800000);
    audio_close(&audio);
    printf("Writes without default-context dispatch: %u, frames: %u, max gap: %.2f ms\n", r.calls,
           r.frames, audio.max_gap / 1000.0);
    if (r.calls < 20 || r.frames < 10000 || r.wrong_thread || audio.max_gap > 200000) {
        fprintf(stderr, "FAIL audio did not progress independently\n");
        return 1;
    }
    gint64 start = g_get_monotonic_time();
    for (int n = 0; n < 20; n++) {
        audio_connect(&audio, bus, n & 1, NULL, NULL, NULL);
        g_usleep((n % 4) * 1000);
        audio_close(&audio);
    }
    printf("20 immediate closes: %.2f ms\n", (g_get_monotonic_time() - start) / 1000.0);
    if (SDL_WasInit(SDL_INIT_AUDIO)) {
        fprintf(stderr, "FAIL immediate close left SDL initialised\n");
        return 1;
    }
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair))
        return 1;
    struct stalled s = {.fd = pair[0], .cancel = g_cancellable_new()};
    GThread *thread = g_thread_new("stalled-peer", authenticate, &s);
    /* The peer intentionally never answers AUTH. Cancellation must interrupt that read. */
    g_usleep(20000);
    start = g_get_monotonic_time();
    g_cancellable_cancel(s.cancel);
    g_thread_join(thread);
    close(pair[1]);
    if (s.conn || !g_error_matches(s.error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        fprintf(stderr, "FAIL peer authentication was not cancelled\n");
        return 1;
    }
    printf("Silent peer authentication cancelled: %.2f ms\n",
           (g_get_monotonic_time() - start) / 1000.0);
    g_clear_error(&s.error);
    g_object_unref(s.cancel);
    g_object_unref(bus);
    puts("PASS independent audio context, immediate teardown and cancelled authentication");
    return 0;
}
