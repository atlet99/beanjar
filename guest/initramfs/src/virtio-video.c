// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The scaler's video windows, lgemu.ko -> webos.video, in the arm64 initramfs.
 *
 * Where the SoC's scaler is a driver of ours (mstar-utopia.ko's XC), the kernel keeps the window
 * table the viewer draws the media in (guest/kernel/modules/lgemu/video.c, include/webos-video.h),
 * and a kernel cannot write a virtio port (the port's file has no write_iter), so this pump copies
 * each frame /dev/lgemu-video gives to the webos.video port. The port is opened at the first
 * frame only: on chips whose windows come from the scaler HAL's shim in tvservice
 * (guest/layers/soc/src/libhal_lg115x), which writes the port itself, the kernel has none and the
 * pump just waits. The port is nonblocking: a frame it cannot take whole is dropped (the next one
 * carries the whole table; the kernel repeats it every second while a window is open), one begun
 * is finished. Errors go to stderr (guest/initramfs/rootfs/init: /run/virtio-video.log). */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../../../include/webos-video.h"

static void sleep_ms(unsigned ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
        ;
}

/* The device of the virtio port called name, or "" when there is none. */
static void find_port(const char *name, char *dev, size_t size)
{
    glob_t paths;
    char want[64];
    dev[0] = 0;
    snprintf(want, sizeof want, "%s\n", name);
    memset(&paths, 0, sizeof paths);
    if (glob("/sys/class/virtio-ports/*/name", 0, NULL, &paths))
        return;
    for (size_t i = 0; i < paths.gl_pathc && !dev[0]; i++) {
        char line[64], leaf[64];
        FILE *f = fopen(paths.gl_pathv[i], "r");
        if (!f)
            continue;
        if (fgets(line, sizeof line, f) && !strcmp(line, want) &&
            sscanf(paths.gl_pathv[i], "/sys/class/virtio-ports/%63[^/]/name", leaf) == 1)
            snprintf(dev, size, "/dev/%s", leaf);
        fclose(f);
    }
    globfree(&paths);
}

/* The whole frame, or nothing if the port cannot take its start now (then FALSE). */
static int send_frame(int fd, const struct webos_video_packet *p)
{
    const char *b = (const char *)p;
    size_t done = 0;
    while (done < sizeof *p) {
        ssize_t k = write(fd, b + done, sizeof *p - done);
        if (k > 0) {
            done += (size_t)k;
            continue;
        }
        if (k < 0 && errno == EINTR)
            continue;
        if (k < 0 && errno == EAGAIN) {
            if (!done)
                return 0;
            struct pollfd w = {fd, POLLOUT, 0};
            poll(&w, 1, 1000);
            continue;
        }
        return -1;
    }
    return 1;
}

int main(void)
{
    char dev[96];
    struct webos_video_packet p;
    int in, out = -1, opened = 0;

    find_port("webos.video", dev, sizeof dev);
    if (!dev[0]) {
        fprintf(stderr, "virtio-video: no webos.video port, so no video windows\n");
        return 0;
    }
    in = open("/dev/lgemu-video", O_RDONLY | O_CLOEXEC);
    if (in < 0) {
        fprintf(stderr, "virtio-video: /dev/lgemu-video: %s (lgemu.ko not loaded)\n",
                strerror(errno));
        return 1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("virtio-video: fork");
        return 1;
    }
    if (pid > 0)
        return 0;
    setsid();
    for (;;) {
        ssize_t n = read(in, &p, sizeof p);
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (n != (ssize_t)sizeof p) {
            fprintf(stderr, "virtio-video: /dev/lgemu-video: %s\n",
                    n < 0 ? strerror(errno) : "short read");
            sleep_ms(1000);
            continue;
        }
        if (out < 0) {
            out = open(dev, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
            if (out < 0) {
                if (!opened++)
                    fprintf(stderr, "virtio-video: %s: %s; retrying\n", dev, strerror(errno));
                continue;
            }
            fprintf(stderr, "virtio-video: /dev/lgemu-video -> %s\n", dev);
        }
        if (send_frame(out, &p) < 0) {
            fprintf(stderr, "virtio-video: %s: %s\n", dev, strerror(errno));
            close(out);
            out = -1;
        }
    }
}
