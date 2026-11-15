// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* bridgetest: what a 32-bit ARM userland needs from the kernel's compat ABI, and so what the
 * qemu-user compat bridge (docs/dev/architecture/qemu-user-bridge.md) must pass through when a
 * guest runs it under qemu-arm (USERLAND=qemu-user): uinput and evdev (16-byte events for a 32-bit
 * reader), DRM and virtio-gpu ioctls, a robust mutex whose owner died, signals that restart or
 * interrupt a blocking read, a real-time signal, fork, exec and wait, and /proc/<pid>/exe naming
 * the program. Natively every check passes too, so it checks either userland.
 *
 *   bridgetest
 *
 * Prints bridge.* key=value lines: bridge.userland (qemu-user when qemu-arm is mapped into its
 * processes, else native), then bridge.<check>=pass or fail with what it saw, and bridge.failed;
 * exits with the number of failed checks. Built static for 32-bit ARM by test_bridge_tcg.py. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#ifndef UI_GET_SYSNAME
#define UI_GET_SYSNAME(len) _IOC(_IOC_READ, UINPUT_IOCTL_BASE, 44, len)
#endif
#ifndef BUS_VIRTUAL
#define BUS_VIRTUAL 0x06
#endif
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails;
#define CHECK(name, cond, ...)                                                                     \
    do {                                                                                           \
        int ok_ = (cond);                                                                          \
        printf("bridge.%s=%s (", name, ok_ ? "pass" : "fail");                                     \
        printf(__VA_ARGS__);                                                                       \
        printf(")\n");                                                                             \
        if (!ok_)                                                                                  \
            fails++;                                                                               \
    } while (0)

/* drm uapi without the headers */
struct drm_version32 {
    int major, minor, patch;
    uint32_t name_len;
    uint32_t name;
    uint32_t date_len;
    uint32_t date;
    uint32_t desc_len;
    uint32_t desc;
};
#define DRM_IOCTL_VERSION32 _IOWR('d', 0x00, struct drm_version32)
struct drm_virtgpu_getparam {
    uint64_t param;
    uint64_t value;
};
#define DRM_IOCTL_VIRTGPU_GETPARAM _IOWR('d', 0x40 + 0x03, struct drm_virtgpu_getparam)
struct drm_mode_card_res {
    uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
    uint32_t count_fbs, count_crtcs, count_connectors, count_encoders, min_width, max_width,
        min_height, max_height;
};
#define DRM_IOCTL_MODE_GETRESOURCES _IOWR('d', 0xA0, struct drm_mode_card_res)

static void test_evdev(void)
{
    int u = open("/dev/uinput", O_RDWR | O_NONBLOCK);
    if (u < 0) {
        CHECK("uinput-open", 0, "errno %d", errno);
        return;
    }
    struct uinput_user_dev ud;
    memset(&ud, 0, sizeof(ud));
    strcpy(ud.name, "bridgetest");
    ud.id.bustype = BUS_VIRTUAL;
    int r1 = ioctl(u, UI_SET_EVBIT, EV_KEY), r2 = ioctl(u, UI_SET_KEYBIT, KEY_A);
    int r3 = write(u, &ud, sizeof(ud)) == sizeof(ud) ? 0 : -1, r4 = ioctl(u, UI_DEV_CREATE);
    CHECK("uinput-create", !r1 && !r2 && !r3 && !r4, "%d %d %d %d errno %d", r1, r2, r3, r4, errno);
    if (r4)
        return;
    char sys[64] = "", path[128];
    int r5 = ioctl(u, UI_GET_SYSNAME(sizeof(sys)), sys);
    CHECK("uinput-sysname", r5 >= 0, "%s (ret %d errno %d)", sys, r5, errno);
    usleep(200000);
    int ev = -1;
    for (int i = 0; i < 32 && ev < 0; i++) {
        char name[64] = "";
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int f = open(path, O_RDONLY | O_NONBLOCK);
        if (f < 0)
            continue;
        if (ioctl(f, EVIOCGNAME(sizeof(name)), name) >= 0 && !strcmp(name, "bridgetest"))
            ev = f;
        else
            close(f);
    }
    CHECK("evdev-find", ev >= 0, "%s", ev >= 0 ? path : "none");
    if (ev < 0)
        return;
    struct input_event e;
    ssize_t n = read(ev, &e, sizeof(e));
    CHECK("evdev-read-empty", n < 0 && errno == EAGAIN,
          "sizeof(event) %zu ret %zd errno %d (EINVAL: 64-bit event size)", sizeof(e), n, errno);
    struct input_event w[2] = {{.type = EV_KEY, .code = KEY_A, .value = 1}, {.type = EV_SYN}};
    n = write(u, w, sizeof(w));
    CHECK("uinput-write", n == (ssize_t)sizeof(w), "ret %zd errno %d", n, errno);
    usleep(50000);
    struct input_event rd[4];
    memset(rd, 0, sizeof(rd));
    n = read(ev, rd, sizeof(rd));
    CHECK("evdev-read",
          n == 2 * (ssize_t)sizeof(e) && rd[0].type == EV_KEY && rd[0].code == KEY_A &&
              rd[0].value == 1,
          "ret %zd type %d code %d value %d", n, rd[0].type, rd[0].code, rd[0].value);
    ioctl(u, UI_DEV_DESTROY);
    close(ev);
    close(u);
}

static void test_drm(void)
{
    int f = open("/dev/dri/card0", O_RDWR);
    if (f < 0) {
        CHECK("drm-open", 0, "errno %d", errno);
        return;
    }
    char name[32] = "";
    struct drm_version32 v = {.name_len = sizeof(name) - 1, .name = (uint32_t)(uintptr_t)name};
    int r = ioctl(f, DRM_IOCTL_VERSION32, &v);
    CHECK("drm-version", r == 0, "%s %d.%d errno %d", name, v.major, v.minor, r ? errno : 0);
    struct drm_mode_card_res res = {0};
    r = ioctl(f, DRM_IOCTL_MODE_GETRESOURCES, &res);
    CHECK("drm-getresources", r == 0, "crtcs %u connectors %u errno %d", res.count_crtcs,
          res.count_connectors, r ? errno : 0);
    int val = -1;
    struct drm_virtgpu_getparam gp = {.param = 1 /* VIRTGPU_PARAM_3D_FEATURES */,
                                      .value = (uintptr_t)&val};
    r = ioctl(f, DRM_IOCTL_VIRTGPU_GETPARAM, &gp);
    CHECK("virtgpu-getparam", r == 0, "3d %d errno %d", val, r ? errno : 0);
    close(f);
}

static pthread_mutex_t robust;
static void *robust_owner(void *a)
{
    (void)a;
    pthread_mutex_lock(&robust);
    return NULL;
}
static void test_robust(void)
{
    pthread_mutexattr_t at;
    pthread_mutexattr_init(&at);
    pthread_mutexattr_setrobust(&at, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&at, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&robust, &at);
    pthread_t t;
    pthread_create(&t, NULL, robust_owner, NULL);
    pthread_join(t, NULL);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    int r = pthread_mutex_timedlock(&robust, &ts);
    CHECK("robust-mutex", r == EOWNERDEAD, "ret %d (%s)", r, strerror(r));
}

static volatile int alarms;
static void on_alarm(int s)
{
    (void)s;
    alarms++;
}
static int pipefd[2];
static void *late_writer(void *a)
{
    (void)a;
    usleep(400000);
    write(pipefd[1], "x", 1);
    return NULL;
}
static void test_signals(void)
{
    struct sigaction sa = {.sa_handler = on_alarm, .sa_flags = SA_RESTART};
    sigaction(SIGALRM, &sa, NULL);
    pipe(pipefd);
    pthread_t t;
    pthread_create(&t, NULL, late_writer, NULL);
    struct itimerval it = {.it_value = {.tv_usec = 100000}};
    setitimer(ITIMER_REAL, &it, NULL);
    char c = 0;
    ssize_t n = read(pipefd[0], &c, 1);
    pthread_join(t, NULL);
    CHECK("signal-restart", n == 1 && c == 'x' && alarms == 1, "read %zd errno %d alarms %d", n,
          n < 0 ? errno : 0, alarms);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    setitimer(ITIMER_REAL, &it, NULL);
    n = read(pipefd[0], &c, 1);
    CHECK("signal-eintr", n < 0 && errno == EINTR && alarms == 2, "read %zd errno %d alarms %d", n,
          errno, alarms);
    /* a blocked RT signal stays pending through a blocking call */
    sigset_t m;
    sigemptyset(&m);
    sigaddset(&m, SIGRTMIN + 1);
    pthread_sigmask(SIG_BLOCK, &m, NULL);
    raise(SIGRTMIN + 1);
    siginfo_t si;
    struct timespec to = {1, 0};
    int s = sigtimedwait(&m, &si, &to);
    CHECK("rt-signal", s == SIGRTMIN + 1, "got %d want %d", s, SIGRTMIN + 1);
}

static void test_exec(const char *self)
{
    pid_t p = fork();
    if (!p) {
        execl(self, "bridgetest-child", "--child", (char *)NULL);
        _exit(99);
    }
    int st = 0;
    waitpid(p, &st, 0);
    CHECK("fork-exec-wait", WIFEXITED(st) && WEXITSTATUS(st) == 42, "status 0x%x", st);
    p = fork();
    if (!p) {
        kill(getpid(), SIGTERM);
        _exit(1);
    }
    waitpid(p, &st, 0);
    CHECK("wait-signal-status", WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM, "status 0x%x", st);
}

/* true if a mapping of this program's processes names a file containing what. qemu-user fakes
 * /proc/self/maps (the guest's own mappings only), so a child's maps are read instead. */
static int mapped(const char *what)
{
    char line[512], path[64];
    int found = 0;
    pid_t c = fork();
    if (c == 0) {
        pause();
        _exit(0);
    }
    if (c < 0)
        return 0;
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)c);
    FILE *f = fopen(path, "r");
    if (f) {
        while (!found && fgets(line, sizeof(line), f))
            found = strstr(line, what) != NULL;
        fclose(f);
    }
    kill(c, SIGKILL);
    waitpid(c, NULL, 0);
    return found;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--child"))
        return strcmp(argv[0], "bridgetest-child") ? 1 : 42;
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct utsname u;
    uname(&u);
    char exe[256] = "";
    readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    printf("bridge.machine=%s\nbridge.exe=%s\nbridge.userland=%s\n", u.machine, exe,
           mapped("qemu-arm") ? "qemu-user" : "native");
    test_evdev();
    test_drm();
    test_robust();
    test_signals();
    test_exec(argc > 0 && strchr(argv[0], '/') ? argv[0] : "/proc/self/exe");
    {
        pid_t c = fork();
        if (!c) {
            pause();
            _exit(0);
        }
        usleep(300000);
        char lp[64], other[256] = "";
        snprintf(lp, sizeof(lp), "/proc/%d/exe", c);
        readlink(lp, other, sizeof(other) - 1);
        CHECK("other-exe", !strstr(other, "qemu"), "%s -> %s", lp, other);
        kill(c, SIGKILL);
        waitpid(c, NULL, 0);
    }
    printf("bridge.failed=%d\n", fails);
    return fails;
}
