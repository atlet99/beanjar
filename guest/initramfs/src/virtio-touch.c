// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* QEMU's virtio multitouch screen -> LG's touch panel ("LGE Touch Device"), in the arm64 initramfs
 * (README.md, "Touch screen"). */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* libim takes a touch device by a name containing this, and slots 0..9 */
#define NAME   "LGE Touch Device"
#define SLOTS  10
#define SOURCE "QEMU Virtio MultiTouch"

static volatile sig_atomic_t stopped;
static int out;
static struct input_absinfo pos[2]; /* the source's ABS_MT_POSITION_X/Y */
/* The source's contacts, by its slot, as the current frame leaves them. */
static struct {
    int down, x, y;
} src[SLOTS];
/* The panel's slots (ours): the contact in each and what the kernel last took for it. The input
 * core drops a value equal to the slot's last one, so a new contact where the slot's last ended
 * would carry no position, and libim would place it wrongly: a new contact goes to a free slot
 * whose last position is another (the least recently used), as a panel's driver picks slots. */
static struct {
    int id;         /* our tracking ID, -1: no contact */
    int x, y;       /* the last position the kernel took (-1: none yet) */
    int from;       /* the source slot whose contact it is */
    unsigned order; /* when it began: the oldest is the emulated pointer, as the kernel's */
    unsigned used;  /* when it last ended */
} panel[SLOTS];
static int slot = -1, touching, next_id;
static unsigned clock_;

static void stop(int sig)
{
    (void)sig;
    stopped = 1;
}
static void fail(const char *what)
{
    perror(what);
    exit(1);
}
static void emit(unsigned short type, unsigned short code, int value)
{
    struct input_event ev = {.type = type, .code = code, .value = value};
    ssize_t n;
    do {
        n = write(out, &ev, sizeof(ev));
    } while (n < 0 && errno == EINTR);
    if (n != sizeof(ev))
        fail("touch write");
}
static void select_slot(int s)
{
    if (s != slot) {
        slot = s;
        emit(EV_ABS, ABS_MT_SLOT, s);
    }
}
static int clampv(int axis, int v)
{
    return v < pos[axis].minimum   ? pos[axis].minimum
           : v > pos[axis].maximum ? pos[axis].maximum
                                   : v;
}
/* the panel slot of the source slot s's contact, -1 if none */
static int panel_of(int s)
{
    for (int i = 0; i < SLOTS; i++)
        if (panel[i].id >= 0 && panel[i].from == s)
            return i;
    return -1;
}
/* a free panel slot for a contact beginning at x, y */
static int free_slot(int x, int y)
{
    int best = -1, fallback = -1;
    for (int i = 0; i < SLOTS; i++) {
        if (panel[i].id >= 0)
            continue;
        if (fallback < 0 || panel[i].used < panel[fallback].used)
            fallback = i;
        if (panel[i].x != x && panel[i].y != y && (best < 0 || panel[i].used < panel[best].used))
            best = i;
    }
    return best >= 0 ? best : fallback;
}
/* The frame as a panel's driver reports it (hid-multitouch's mt_sync_frame): each changed slot's
 * ABS_MT_SLOT, ABS_MT_TRACKING_ID and position, then BTN_TOUCH and the oldest contact as
 * ABS_X/ABS_Y (input_mt_report_pointer_emulation), then SYN_REPORT. */
static void frame(void)
{
    for (int s = 0; s < SLOTS; s++) {
        int p = panel_of(s);
        if (p >= 0 && !src[s].down) {
            select_slot(p);
            panel[p].id = -1;
            panel[p].used = clock_++;
            emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
            continue;
        }
        if (!src[s].down)
            continue;
        int x = clampv(0, src[s].x), y = clampv(1, src[s].y);
        if (p < 0) {
            p = free_slot(x, y);
            if (p < 0)
                continue; /* cannot happen: as many panel slots as source ones */
            select_slot(p);
            panel[p].id = next_id;
            panel[p].from = s;
            panel[p].order = clock_++;
            next_id = (next_id + 1) & 0xffff;
            emit(EV_ABS, ABS_MT_TRACKING_ID, panel[p].id);
        }
        if (x != panel[p].x) {
            select_slot(p);
            emit(EV_ABS, ABS_MT_POSITION_X, x);
            panel[p].x = x;
        }
        if (y != panel[p].y) {
            select_slot(p);
            emit(EV_ABS, ABS_MT_POSITION_Y, y);
            panel[p].y = y;
        }
    }
    int oldest = -1;
    for (int i = 0; i < SLOTS; i++)
        if (panel[i].id >= 0 && (oldest < 0 || panel[i].order < panel[oldest].order))
            oldest = i;
    if (touching != (oldest >= 0)) {
        touching = oldest >= 0;
        emit(EV_KEY, BTN_TOUCH, touching);
    }
    if (oldest >= 0) {
        emit(EV_ABS, ABS_X, panel[oldest].x);
        emit(EV_ABS, ABS_Y, panel[oldest].y);
    }
    emit(EV_SYN, SYN_REPORT, 0);
}
static void release_all(void)
{
    for (int s = 0; s < SLOTS; s++)
        src[s].down = 0;
    frame();
}
static int source(void)
{
    glob_t paths = {0};
    int found = -1;
    if (glob("/dev/input/event*", 0, NULL, &paths) == 0) {
        for (size_t i = 0; i < paths.gl_pathc; i++) {
            char name[256] = {0};
            int fd = open(paths.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0)
                continue;
            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && !strcmp(name, SOURCE)) {
                for (int a = 0; a < 2; a++)
                    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X + a), &pos[a]) < 0 ||
                        pos[a].maximum <= pos[a].minimum)
                        fail("multitouch axis");
                if (ioctl(fd, EVIOCGRAB, 1) < 0)
                    fail("grab multitouch");
                found = fd;
                break;
            }
            close(fd);
        }
    }
    globfree(&paths);
    return found;
}
/* after SYN_DROPPED: the source's slots as they are now (EVIOCGMTSLOTS) */
static void reconcile(int in)
{
    struct {
        __u32 code;
        __s32 values[SLOTS];
    } ids = {.code = ABS_MT_TRACKING_ID}, xs = {.code = ABS_MT_POSITION_X},
      ys = {.code = ABS_MT_POSITION_Y};
    if (ioctl(in, EVIOCGMTSLOTS(sizeof(ids)), &ids) < 0 ||
        ioctl(in, EVIOCGMTSLOTS(sizeof(xs)), &xs) < 0 ||
        ioctl(in, EVIOCGMTSLOTS(sizeof(ys)), &ys) < 0) {
        release_all();
        return;
    }
    for (int s = 0; s < SLOTS; s++) {
        src[s].down = ids.values[s] >= 0;
        src[s].x = xs.values[s];
        src[s].y = ys.values[s];
    }
    frame();
}
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "usage: virtio-touch\n");
        return 1;
    }
    int in = source();
    if (in < 0) {
        fprintf(stderr, "no %s\n", SOURCE);
        return 1;
    }
    out = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
    if (out < 0)
        fail("open uinput");
    if (ioctl(out, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(out, UI_SET_KEYBIT, BTN_TOUCH) < 0 ||
        ioctl(out, UI_SET_EVBIT, EV_ABS) < 0 || ioctl(out, UI_SET_PROPBIT, INPUT_PROP_DIRECT) < 0)
        fail("touch capabilities");
    struct uinput_abs_setup abs[] = {
        {.code = ABS_X, .absinfo = {.minimum = pos[0].minimum, .maximum = pos[0].maximum}},
        {.code = ABS_Y, .absinfo = {.minimum = pos[1].minimum, .maximum = pos[1].maximum}},
        {.code = ABS_MT_SLOT, .absinfo = {.maximum = SLOTS - 1}},
        {.code = ABS_MT_TRACKING_ID, .absinfo = {.maximum = 0xffff}},
        {.code = ABS_MT_POSITION_X,
         .absinfo = {.minimum = pos[0].minimum, .maximum = pos[0].maximum}},
        {.code = ABS_MT_POSITION_Y,
         .absinfo = {.minimum = pos[1].minimum, .maximum = pos[1].maximum}},
    };
    for (size_t i = 0; i < sizeof(abs) / sizeof(abs[0]); i++)
        if (ioctl(out, UI_SET_ABSBIT, abs[i].code) < 0 || ioctl(out, UI_ABS_SETUP, &abs[i]) < 0)
            fail("touch axis");
    /* the StanbyME's panel: USB HID 1fd2:0205 (the firmware's 99-hid-modules.rules); libim keeps
     * the IDs as information only */
    struct uinput_setup setup = {.id = {.bustype = BUS_USB, .vendor = 0x1fd2, .product = 0x0205},
                                 .name = NAME};
    if (ioctl(out, UI_DEV_SETUP, &setup) < 0 || ioctl(out, UI_DEV_CREATE) < 0)
        fail("create touch device");
    for (int i = 0; i < SLOTS; i++) {
        panel[i].id = -1;
        panel[i].x = panel[i].y = -1;
    }
    pid_t pid = fork();
    if (pid < 0)
        fail("fork");
    if (pid > 0) {
        close(out);
        close(in);
        return 0;
    }
    if (setsid() < 0)
        fail("setsid");
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    /* the source's current slot: events of a slot libim does not take (10 on) are dropped */
    int source_slot = 0, dropped = 0;
    while (!stopped) {
        if (in < 0) {
            in = source();
            if (in < 0) {
                sleep(1);
                continue;
            }
            source_slot = 0;
        }
        struct pollfd p = {.fd = in, .events = POLLIN};
        int ready = poll(&p, 1, 1000);
        if (ready < 0 && errno != EINTR)
            fail("poll multitouch");
        if (ready <= 0)
            continue;
        struct input_event ev;
        ssize_t n = read(in, &ev, sizeof(ev));
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n != sizeof(ev)) {
            release_all();
            close(in);
            in = -1;
            dropped = 0;
            continue;
        }
        if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
            dropped = 1;
            continue;
        }
        if (dropped) {
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                reconcile(in);
                dropped = 0;
            }
            continue;
        }
        if (ev.type == EV_SYN && ev.code == SYN_REPORT)
            frame();
        else if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT)
            source_slot = ev.value;
        else if (ev.type == EV_ABS && source_slot >= 0 && source_slot < SLOTS) {
            if (ev.code == ABS_MT_TRACKING_ID)
                src[source_slot].down = ev.value >= 0;
            else if (ev.code == ABS_MT_POSITION_X)
                src[source_slot].x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y)
                src[source_slot].y = ev.value;
        }
        /* BTN_TOUCH is the frame's (QEMU may route its button to another device); other
         * buttons are the pointer's */
    }
    release_all();
    if (in >= 0)
        close(in);
    ioctl(out, UI_DEV_DESTROY);
    close(out);
    return 0;
}
