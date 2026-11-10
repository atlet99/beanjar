// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libhal_lg115x.so.0 for the emulator: the video scaler and panel HAL functions behind tvservice's
 * video windows, and the webos.video channel to the host viewer.
 *
 * tvservice drives the video plane through the scaler (VSC) windows, 0 = MAIN and 1 = SUB
 * (DIL_VSC_* in tvservice, one HAL call each):
 *   HAL_VSC_Open(wId) / HAL_VSC_Close(wId)
 *   HAL_VSC_Connect(wId, VSC_INPUT_SRC_INFO_T {type, attr}, VSC_OUTPUT_MODE_T) / _Disconnect
 *       the struct is passed by value (r1 = type, r2 = attr): VSC_INPUTSRC_AVD 0, ADC 1, HDMI 2,
 *       VDEC 3, JPEG 4; a media pipeline is VDEC with its port (videosinkmanager/connect), Live TV
 *       in ATV is AVD
 *   HAL_VSC_SetOutputRegion(wId, VIDEO_RECT_T) / SetInputRegion
 *       {UINT16 x, y, w, h} by value (r1 = x | y << 16, r2 = w | h << 16), the output in display
 *       pixels (tv.display/setDisplayWindow after video_calcOsdRectToVideoRect), the input the
 *       source crop (0 x 0: the whole source)
 *   HAL_VSC_SetWinBlank(wId, on, VIDEO_DDI_WIN_COLOR_T)
 *       the window's video mute: tv.avblock/startAvMute and stopAvMute, around a connect and a
 *       disconnect, and while an input has no signal
 *   HAL_VBE_DISP_SetMute(on, ...)
 *       the panel's video mute: tv.power/turnOffAv and turnOnAv
 * Stock LG answers Open/Close/Connect/Disconnect with 0 without a driver and the others with -5
 * (not supported); tvservice ignores most of those failures, but tv.power/turnOnAv and turnOffAv
 * answer POWER_ERROR_0005 on the panel mute's. Here Open/Close/Connect/Disconnect still run LG's
 * code (their result is kept), the others answer OK and are recorded, and the getters answer the
 * recorded regions.
 *
 * The recorded state goes to the viewer in the webos.video format of video milestone 1
 * (include/webos-video.h): one context per open window, named MAIN or SUB; registered = open,
 * connected = connected, av_mute = blanked, panel-muted, or an input with no picture in the
 * emulator (VDEC has one, the media pipeline's stub sinks, and HDMI where an H13's UHD companion
 * feeds it; the tuner's analog decoder has no signal and no external source is connected), the
 * window = the output region,
 * the stream size = the input region when one is set, and the input with its port (a VDEC port is
 * the media pipeline's vdec-ch, which the viewer matches with its media streams). The viewer draws
 * the decoded video where a window is shown (connected, with a window, not muted). Every change is
 * sent with its event and the table as that change left it, and the table is repeated every second
 * while a window is open, from a thread of its own: the port is nonblocking, a frame the port
 * cannot take is dropped (the next one is complete), and a frame begun is finished. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tvservice-hal.h"
#include "../../../../../include/webos-video.h"

#define HAL_OK         0
#define WINDOWS        2
#define VSC_INPUT_VDEC 3
#define SCREEN_W       1920 /* the display without tvservice's answer (below) */
#define SCREEN_H       1080

static const char *const input_names[] = {"AVD", "ADC", "HDMI", "VDEC", "JPEG"};

struct window {
    int open, connected, blank;
    unsigned input, attr;
    int has_out;
    unsigned x, y, w, h; /* output region */
    unsigned in_w, in_h; /* input region size */
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static struct window windows[WINDOWS];
static int panel_mute;
#define VSC_INPUT_HDMI 2
/* frames to send: the table as each change left it, oldest first (a full queue drops its
 * oldest frame; the newest always has the current table) */
#define QUEUE 16
static struct webos_video_packet queue[QUEUE];
static unsigned queue_head, queue_len;
static uint32_t sequence;
static int started;

typedef int fn4(unsigned, unsigned, unsigned, unsigned);

static fn4 *stock(const char *name)
{
    fn4 *f = (fn4 *)dlsym(RTLD_NEXT, name);
    if (!f)
        hal_log("video: stock %s not found", name);
    return f;
}

/* ---- the table, as the viewer sees it ---- */

/* The display the regions are in: tvservice's own OSA_MD_GetDisplayResolution (w << 12 | h:
 * 0x780438 is 1920x1080, 0xf00870 3840x2160), which it scales the OSD rectangles to. On WT1H and
 * W15M it is at most the OSD's 1920x1080 (H13 shows a UHD board's pictures through its
 * companion); on W17H it is the panel's, 3840x2160 for a UHD board (tvconfig's panel resolution).
 */
static void display_size(uint32_t *w, uint32_t *h)
{
    static uint32_t (*get)(void);
    static int looked;
    static uint32_t last;
    uint32_t v, sw, sh;
    if (!looked) {
        get = (uint32_t (*)(void))dlsym(RTLD_DEFAULT, "OSA_MD_GetDisplayResolution");
        looked = 1;
    }
    /* asked each time: it reads tvservice's configuration, which may not be loaded yet at the
     * first window */
    v = get ? get() : 0;
    sw = v >> 12 & 0xfff;
    sh = v & 0xfff;
    if (sw < 640 || sh < 360) {
        sw = SCREEN_W;
        sh = SCREEN_H;
    }
    if ((sw << 12 | sh) != last) {
        last = sw << 12 | sh;
        hal_log("video: display %ux%u%s", sw, sh, get ? "" : " (no OSA_MD_GetDisplayResolution)");
    }
    *w = sw;
    *h = sh;
}

static void context_of(unsigned i, struct webos_video_context *c)
{
    uint32_t sw, sh;
    const struct window *w = &windows[i];
    memset(c, 0, sizeof *c);
    snprintf(c->context, sizeof c->context, "%s", i ? "SUB" : "MAIN");
    c->registered = w->open;
    c->connected = w->connected;
    /* HDMI too: on an H13 UHD board the companion's pictures arrive there (the viewer draws a
     * media stream that names it, and nothing else: no external source is ever connected) */
    c->av_mute =
        w->blank || panel_mute || (w->input != VSC_INPUT_VDEC && w->input != VSC_INPUT_HDMI);
    c->has_rect = w->has_out;
    c->x = (int32_t)w->x;
    c->y = (int32_t)w->y;
    c->w = w->w;
    c->h = w->h;
    display_size(&sw, &sh);
    c->full_screen = w->has_out && !w->x && !w->y && w->w == sw && w->h == sh;
    c->width = w->in_w;
    c->height = w->in_h;
    c->input = w->input;
    c->input_port = w->attr;
}

static int shown(const struct webos_video_context *c)
{
    return c->connected && c->has_rect && !c->av_mute && c->w && c->h;
}

/* called with lock held: the open windows; *subject_index is set to the place of window wid */
static unsigned table(struct webos_video_context *out, unsigned wid, unsigned *subject_index)
{
    unsigned n = 0;
    for (unsigned i = 0; i < WINDOWS; i++) {
        if (!windows[i].open)
            continue;
        if (subject_index && wid == i)
            *subject_index = n;
        context_of(i, &out[n++]);
    }
    return n;
}

/* called with lock held: the current table as a frame */
static void frame(struct webos_video_packet *p, enum webos_video_event event, unsigned wid)
{
    unsigned subject = WEBOS_VIDEO_MAX;
    memset(p, 0, sizeof *p);
    memcpy(p->magic, WEBOS_VIDEO_MAGIC, 8);
    p->bytes = sizeof *p;
    p->sequence = ++sequence;
    p->event = (uint32_t)event;
    p->count = table(p->contexts, wid, &subject);
    p->subject = event == WEBOS_VIDEO_SYNC ? WEBOS_VIDEO_MAX : subject;
    display_size(&p->screen_w, &p->screen_h);
}

unsigned tvservice_hal_video_shown(struct webos_video_context *out, unsigned max)
{
    struct webos_video_context all[WINDOWS];
    unsigned n = 0, count;
    pthread_mutex_lock(&lock);
    count = table(all, WINDOWS, NULL);
    pthread_mutex_unlock(&lock);
    for (unsigned i = 0; i < count && n < max; i++)
        if (shown(&all[i]))
            out[n++] = all[i];
    return n;
}

/* ---- the webos.video port ---- */

static int open_port(void)
{
    glob_t paths;
    int fd = -1;
    memset(&paths, 0, sizeof paths);
    if (!glob("/sys/class/virtio-ports/*/name", 0, NULL, &paths)) {
        for (size_t i = 0; i < paths.gl_pathc && fd < 0; i++) {
            char name[64], leaf[64], dev[96];
            FILE *f = fopen(paths.gl_pathv[i], "r");
            int match;
            if (!f)
                continue;
            match = fgets(name, sizeof name, f) && !strcmp(name, "webos.video\n");
            fclose(f);
            if (match &&
                sscanf(paths.gl_pathv[i], "/sys/class/virtio-ports/%63[^/]/name", leaf) == 1) {
                snprintf(dev, sizeof dev, "/dev/%s", leaf);
                fd = open(dev, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
            }
        }
    }
    globfree(&paths);
    return fd;
}

/* One whole frame, or nothing: a frame the port cannot take at all is dropped, one begun is
 * finished (up to 2 s; the viewer resynchronises on a cut frame by reconnecting). */
static void write_frame(int *fd, const struct webos_video_packet *p)
{
    const char *b = (const char *)p;
    size_t off = 0;
    int waited = 0;
    if (*fd < 0 && (*fd = open_port()) < 0)
        return;
    while (off < sizeof *p) {
        ssize_t n = write(*fd, b + off, sizeof *p - off);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN && off && waited < 20) {
            struct pollfd pfd = {*fd, POLLOUT, 0};
            poll(&pfd, 1, 100);
            waited++;
            continue;
        }
        if (off || (n < 0 && errno != EAGAIN)) {
            close(*fd);
            *fd = -1;
        }
        return;
    }
}

static void *video_thread(void *arg)
{
    static struct webos_video_packet p;
    int fd = -1;
    (void)arg;
    pthread_mutex_lock(&lock);
    for (;;) {
        if (queue_len) {
            p = queue[queue_head];
            queue_head = (queue_head + 1) % QUEUE;
            queue_len--;
        } else {
            /* nothing new: repeat the table every second while a window is open */
            struct webos_video_context tmp[WINDOWS];
            struct timespec t;
            clock_gettime(CLOCK_REALTIME, &t);
            t.tv_sec += 1;
            if (table(tmp, WINDOWS, NULL))
                pthread_cond_timedwait(&wake, &lock, &t);
            else
                pthread_cond_wait(&wake, &lock);
            if (queue_len || !table(tmp, WINDOWS, NULL))
                continue;
            frame(&p, WEBOS_VIDEO_SYNC, WINDOWS);
        }
        pthread_mutex_unlock(&lock);
        write_frame(&fd, &p);
        pthread_mutex_lock(&lock);
    }
    return NULL;
}

/* called with lock held */
static void changed(enum webos_video_event event, unsigned wid)
{
    static const char *const names[] = {"sync",  "open", "connect",    "window",
                                        "input", "mute", "disconnect", "close"};
    const struct window *w = &windows[wid];
    hal_log("video: %s %s: open=%d connected=%d input=%s%u blank=%d panel-mute=%d "
            "window=%s%u,%u %ux%u source=%ux%u",
            names[event], wid ? "SUB" : "MAIN", w->open, w->connected,
            w->input < 5 ? input_names[w->input] : "?", w->attr, w->blank, panel_mute,
            w->has_out ? "" : "(none) ", w->x, w->y, w->w, w->h, w->in_w, w->in_h);
    if (!started) {
        pthread_t t;
        pthread_attr_t attr;
        started = 1;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&attr, 64 * 1024);
        if (pthread_create(&t, &attr, video_thread, NULL))
            hal_log("video: cannot start the webos.video thread");
        pthread_attr_destroy(&attr);
    }
    if (queue_len == QUEUE) {
        queue_head = (queue_head + 1) % QUEUE;
        queue_len--;
    }
    frame(&queue[(queue_head + queue_len) % QUEUE], event, wid);
    queue_len++;
    pthread_cond_signal(&wake);
}

/* windows other than MAIN and SUB are not tracked */
#define WINDOW(wid, rc)                                                                            \
    do {                                                                                           \
        wid &= 0xff;                                                                               \
        if (wid >= WINDOWS)                                                                        \
            return rc;                                                                             \
    } while (0)

/* ---- HAL_VSC ---- */

int HAL_VSC_Open(unsigned wid)
{
    static fn4 *f;
    int rc;
    if (!f && !(f = stock("HAL_VSC_Open")))
        return -1;
    rc = f(wid, 0, 0, 0);
    WINDOW(wid, rc);
    if (!rc) {
        pthread_mutex_lock(&lock);
        windows[wid].open = 1;
        changed(WEBOS_VIDEO_REGISTER, wid);
        pthread_mutex_unlock(&lock);
    }
    return rc;
}

int HAL_VSC_Close(unsigned wid)
{
    static fn4 *f;
    int rc;
    if (!f && !(f = stock("HAL_VSC_Close")))
        return -1;
    rc = f(wid, 0, 0, 0);
    WINDOW(wid, rc);
    if (!rc) {
        pthread_mutex_lock(&lock);
        /* a closed window has no picture; its next user sets the regions again */
        windows[wid].open = windows[wid].connected = windows[wid].has_out = 0;
        windows[wid].in_w = windows[wid].in_h = 0;
        changed(WEBOS_VIDEO_UNREGISTER, wid);
        pthread_mutex_unlock(&lock);
    }
    return rc;
}

int HAL_VSC_Connect(unsigned wid, unsigned input, unsigned attr, unsigned mode)
{
    static fn4 *f;
    int rc;
    if (!f && !(f = stock("HAL_VSC_Connect")))
        return -1;
    rc = f(wid, input, attr, mode);
    WINDOW(wid, rc);
    if (rc)
        hal_log("video: stock HAL_VSC_Connect(%u, %u, %u, %u) = %d", wid, input, attr, mode, rc);
    if (!rc) {
        pthread_mutex_lock(&lock);
        windows[wid].connected = 1;
        windows[wid].input = input;
        windows[wid].attr = attr & 0xff;
        changed(WEBOS_VIDEO_CONNECT, wid);
        pthread_mutex_unlock(&lock);
    }
    return rc;
}

int HAL_VSC_Disconnect(unsigned wid, unsigned input, unsigned attr, unsigned mode)
{
    static fn4 *f;
    int rc;
    if (!f && !(f = stock("HAL_VSC_Disconnect")))
        return -1;
    rc = f(wid, input, attr, mode);
    WINDOW(wid, rc);
    if (!rc) {
        pthread_mutex_lock(&lock);
        windows[wid].connected = 0;
        changed(WEBOS_VIDEO_DISCONNECT, wid);
        pthread_mutex_unlock(&lock);
    }
    return rc;
}

int HAL_VSC_SetOutputRegion(unsigned wid, unsigned xy, unsigned wh)
{
    WINDOW(wid, HAL_OK);
    pthread_mutex_lock(&lock);
    struct window *w = &windows[wid];
    if (!w->has_out || w->x != (xy & 0xffff) || w->y != xy >> 16 || w->w != (wh & 0xffff) ||
        w->h != wh >> 16) {
        w->has_out = 1;
        w->x = xy & 0xffff;
        w->y = xy >> 16;
        w->w = wh & 0xffff;
        w->h = wh >> 16;
        changed(WEBOS_VIDEO_RECT, wid);
    }
    pthread_mutex_unlock(&lock);
    return HAL_OK;
}

int HAL_VSC_SetInputRegion(unsigned wid, unsigned xy, unsigned wh)
{
    (void)xy;
    WINDOW(wid, HAL_OK);
    pthread_mutex_lock(&lock);
    struct window *w = &windows[wid];
    if (w->in_w != (wh & 0xffff) || w->in_h != wh >> 16) {
        w->in_w = wh & 0xffff;
        w->in_h = wh >> 16;
        changed(WEBOS_VIDEO_INFO, wid);
    }
    pthread_mutex_unlock(&lock);
    return HAL_OK;
}

static int get_region(unsigned wid, uint16_t *rect, int output)
{
    if (!rect)
        return -1;
    wid &= 0xff;
    memset(rect, 0, 8);
    if (wid >= WINDOWS)
        return HAL_OK;
    pthread_mutex_lock(&lock);
    const struct window *w = &windows[wid];
    if (output) {
        rect[0] = (uint16_t)w->x;
        rect[1] = (uint16_t)w->y;
        rect[2] = (uint16_t)w->w;
        rect[3] = (uint16_t)w->h;
    } else {
        rect[2] = (uint16_t)w->in_w;
        rect[3] = (uint16_t)w->in_h;
    }
    pthread_mutex_unlock(&lock);
    return HAL_OK;
}

int HAL_VSC_GetOutputRegion(unsigned wid, uint16_t *rect)
{
    return get_region(wid, rect, 1);
}
int HAL_VSC_GetInputRegion(unsigned wid, uint16_t *rect)
{
    return get_region(wid, rect, 0);
}

int HAL_VSC_SetWinBlank(unsigned wid, unsigned on, unsigned color)
{
    (void)color;
    WINDOW(wid, HAL_OK);
    on &= 0xff;
    pthread_mutex_lock(&lock);
    if (windows[wid].blank != !!on) {
        windows[wid].blank = !!on;
        changed(WEBOS_VIDEO_MUTE, wid);
    }
    pthread_mutex_unlock(&lock);
    return HAL_OK;
}

int HAL_VSC_SetWinFreeze(unsigned wid, unsigned on)
{
    (void)wid;
    (void)on;
    return HAL_OK;
}

/* ---- the panel ---- */

int HAL_VBE_DISP_SetMute(unsigned on)
{
    on &= 0xff;
    pthread_mutex_lock(&lock);
    if (panel_mute != !!on) {
        panel_mute = !!on;
        for (unsigned i = 0; i < WINDOWS; i++)
            if (windows[i].open) {
                changed(WEBOS_VIDEO_MUTE, i);
                break;
            }
        hal_log("video: panel %s", on ? "muted" : "unmuted");
    }
    pthread_mutex_unlock(&lock);
    return HAL_OK;
}
