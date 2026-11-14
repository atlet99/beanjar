// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The test guest's input check: a key press on the VM's virtio keyboard, which the host injects
 * (the test command, QMP input-send-event: the device the viewer's and VNC's keys reach).
 *
 *   tg-input READY_FILE [SECONDS]
 *
 * Finds the evdev node named "QEMU Virtio Keyboard", then creates READY_FILE (on the out share: the
 * host's cue to start sending keys) and waits up to SECONDS (default 60) for a key event.
 * Prints input.* key=value lines; exits 0 once a key went down. */
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: tg-input READY_FILE [SECONDS]\n");
        return 2;
    }
    int seconds = argc > 2 ? atoi(argv[2]) : 60;
    int fd = -1;
    char path[300], name[128] = "";
    DIR *d = opendir("/dev/input");
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5))
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int f = open(path, O_RDONLY | O_CLOEXEC);
        if (f < 0)
            continue;
        if (ioctl(f, EVIOCGNAME(sizeof name), name) > 0 && strstr(name, "Keyboard")) {
            fd = f;
            break;
        }
        close(f);
    }
    if (d)
        closedir(d);
    if (fd < 0) {
        printf("input.error=no virtio keyboard in /dev/input\n");
        return 1;
    }
    printf("input.device=%s\n", name);

    FILE *ready = fopen(argv[1], "w");
    if (!ready) {
        printf("input.error=cannot create %s\n", argv[1]);
        return 1;
    }
    fclose(ready);
    double t0 = now_ms();
    for (;;) {
        int left = (int)(t0 + seconds * 1000.0 - now_ms());
        struct pollfd p = {fd, POLLIN, 0};
        if (left <= 0 || poll(&p, 1, left) <= 0)
            break;
        struct input_event ev;
        if (read(fd, &ev, sizeof ev) != sizeof ev)
            break;
        if (ev.type == EV_KEY && ev.value == 1) {
            printf("input.key=%u\n", ev.code);
            printf("input.wait_ms=%.1f\n", now_ms() - t0);
            return 0;
        }
    }
    printf("input.error=no key within %d s\n", seconds);
    return 1;
}
