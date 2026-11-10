// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The antenna, webos.tuner <-> dtv_soc.ko, in the arm64 initramfs.
 *
 * The host's tuner (host/core/beanjar/tuner.py) sends its channel map and the transport stream
 * of the tuned frequency over the webos.tuner virtio-serial port, and learns which frequency
 * frontend 0 is tuned to (include/webos-tuner.h). The kernel cannot read a virtio port itself (the
 * port's file has no read_iter), so this pump copies the port's stream to /dev/dtv-soc
 * (guest/kernel/modules/dtv_soc), whose parser takes it apart, and the device's tune frames to
 * the port. A hang-up of the port means no host program is connected: /dev/dtv-soc is closed then
 * (its parser starts afresh with the next host, which sends its map first, and dtv_soc answers it
 * with the tune), and the port is looked at again a little later.
 * Without dtv_soc.ko (a board with no tuner of ours) or the port, it does nothing. Errors go to
 * stderr (guest/initramfs/rootfs/init: /run/virtio-tuner.log). */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

/* all of buf to fd; 0, or -1 */
static int write_all(int fd, const unsigned char *buf, size_t n)
{
    for (size_t done = 0; done < n;) {
        ssize_t k = write(fd, buf + done, n - done);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            return -1;
        done += (size_t)k;
    }
    return 0;
}

int main(void)
{
    static unsigned char buf[65536];
    char dev[96];
    int port, soc = -1, host = 1, failed = 0;

    find_port("webos.tuner", dev, sizeof dev);
    if (!dev[0])
        return 0;
    if (access("/dev/dtv-soc", F_OK)) {
        fprintf(stderr, "virtio-tuner: no /dev/dtv-soc (dtv_soc.ko not loaded), so no signal\n");
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("virtio-tuner: fork");
        return 1;
    }
    if (pid > 0)
        return 0;
    setsid();
    while ((port = open(dev, O_RDWR | O_CLOEXEC)) < 0) {
        if (!failed++)
            fprintf(stderr, "virtio-tuner: %s: %s; retrying\n", dev, strerror(errno));
        sleep_ms(1000);
    }
    fprintf(stderr, "virtio-tuner: %s <-> /dev/dtv-soc\n", dev);
    for (;;) {
        if (soc < 0 && (soc = open("/dev/dtv-soc", O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0) {
            fprintf(stderr, "virtio-tuner: /dev/dtv-soc: %s\n", strerror(errno));
            sleep_ms(1000);
            continue;
        }
        /* without a host program the port only hangs up: look at it again 200 ms later */
        struct pollfd p[2] = {{soc, POLLIN, 0}, {host ? port : -1, POLLIN, 0}};
        if (poll(p, 2, host ? -1 : 200) < 0) {
            if (errno != EINTR)
                sleep_ms(1000);
            continue;
        }
        if (p[0].revents & POLLIN) {
            ssize_t n = read(soc, buf, sizeof buf);
            /* the tune, for the host; lost if none is there (it learns it with its map) */
            if (n > 0 && host)
                write_all(port, buf, (size_t)n);
        }
        if (!host) {
            host = 1;
            continue;
        }
        if (!(p[1].revents & (POLLIN | POLLHUP | POLLERR)))
            continue;
        ssize_t n = (p[1].revents & POLLIN) ? read(port, buf, sizeof buf) : 0;
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(soc); /* the host went away: its next stream starts afresh */
            soc = -1;
            host = 0;
            continue;
        }
        if (write_all(soc, buf, (size_t)n) < 0)
            fprintf(stderr, "virtio-tuner: /dev/dtv-soc: %s\n", strerror(errno));
    }
}
