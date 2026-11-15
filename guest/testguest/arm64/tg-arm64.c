// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* tg-arm64: a 64-bit ARM program on the 32-bit test guest, the way newer webOS firmware ships
 * them (queue and spacebuns: an AArch64 glibc in /usr/lib64, the loader
 * /usr/lib64/ld-linux-aarch64.so.1). Dynamically linked against that glibc; checks that it runs as
 * AArch64, that ordinary system calls work (uname, clock_gettime, a file written, read back and
 * removed, a thread, fork and wait) and that an ioctl on a device node reaches its driver: DRM's
 * version query on the render node (virtio_gpu).
 *
 *   tg-arm64 [DIR]      DIR: where the file check writes (default /tmp)
 *
 * Prints arm64.* key=value lines: arm64.machine, arm64.<check>=pass or fail with what it saw, and
 * arm64.failed; exits with the number of failed checks. */
#define _GNU_SOURCE
#include <drm/drm.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails;
#define CHECK(name, cond, ...)                                                                     \
    do {                                                                                           \
        int ok_ = (cond);                                                                          \
        printf("arm64.%s=%s (", name, ok_ ? "pass" : "fail");                                      \
        printf(__VA_ARGS__);                                                                       \
        printf(")\n");                                                                             \
        if (!ok_)                                                                                  \
            fails++;                                                                               \
    } while (0)

static void *thread_main(void *arg)
{
    return (char *)arg + 1;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct utsname u;
    int r = uname(&u);
    printf("arm64.machine=%s\n", r == 0 ? u.machine : "?");
    CHECK("abi", sizeof(void *) == 8 && sizeof(long) == 8, "pointer %zu bytes", sizeof(void *));
    CHECK("uname", r == 0 && strcmp(u.machine, "aarch64") == 0, "%s %s", r == 0 ? u.sysname : "?",
          r == 0 ? u.release : "?");

    struct timespec t0, t1;
    r = clock_gettime(CLOCK_MONOTONIC, &t0) | clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK("clock",
          r == 0 && (t1.tv_sec > t0.tv_sec || (t1.tv_sec == t0.tv_sec && t1.tv_nsec >= t0.tv_nsec)),
          "monotonic %lld.%09ld", (long long)t1.tv_sec, t1.tv_nsec);

    char path[4096], buf[16] = {0};
    snprintf(path, sizeof path, "%s/tg-arm64-XXXXXX", argc > 1 ? argv[1] : "/tmp");
    int fd = mkstemp(path);
    ssize_t w = fd >= 0 ? write(fd, "beanjar-arm64", 13) : -1;
    ssize_t n = fd >= 0 && lseek(fd, 0, SEEK_SET) == 0 ? read(fd, buf, sizeof buf - 1) : -1;
    int gone = fd >= 0 && close(fd) == 0 && unlink(path) == 0;
    CHECK("file", w == 13 && n == 13 && strcmp(buf, "beanjar-arm64") == 0 && gone,
          "wrote %zd read %zd errno %d", w, n, errno);

    pthread_t th;
    void *ret = NULL;
    static char mark[2];
    r = pthread_create(&th, NULL, thread_main, mark);
    if (r == 0)
        r = pthread_join(th, &ret);
    CHECK("thread", r == 0 && ret == mark + 1, "error %d", r);

    pid_t pid = fork();
    if (pid == 0)
        _exit(42);
    int status = 0;
    pid_t got = pid > 0 ? waitpid(pid, &status, 0) : -1;
    CHECK("fork", got == pid && WIFEXITED(status) && WEXITSTATUS(status) == 42, "pid %d status %d",
          (int)pid, status);

    /* an ioctl on a device node: DRM's version query on the render node */
    char name[64] = {0};
    struct drm_version v = {.name_len = sizeof name - 1, .name = name};
    fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    r = fd >= 0 ? ioctl(fd, DRM_IOCTL_VERSION, &v) : -1;
    CHECK("drm-version", r == 0 && strcmp(name, "virtio_gpu") == 0, "%s %d.%d errno %d", name,
          v.version_major, v.version_minor, r ? errno : 0);
    if (fd >= 0)
        close(fd);

    printf("arm64.failed=%d\n", fails);
    return fails;
}
