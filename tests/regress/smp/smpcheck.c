// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* smpcheck: a StarfishMediaAPIs (SMP) client shaped like ss4s's player (moonlight-tv) on webOS 3
 * and 4: a full-screen transparent window (as SDL's webOS Wayland window: wl_shell toplevel and
 * webOS shell surface with its appId; the video plane shows through), which makes the app the
 * foreground one that ACB serves; an ACB client (libAcbAPI: the media's place on the screen, as
 * ss4s's smp_resource_acb.c), notifyForeground and the media id to ACB, Load with a BUFFERSTREAM
 * H.264 payload, the display window (full screen), Feed the clip's access units at 30 fps with
 * their PTS; at LOADCOMPLETED the main sink and the loaded state to ACB, Play and the playing
 * state; then nothing for 3 s (starved), pushEOS, Unload. Every callback goes to syslog (SMPCHECK,
 * PmLog's /var/log/messages) with the ms since the first feed; at the end one line sums up what an
 * app gets: the FRAMEREADY (type 0) callbacks from the first feed to pushEOS, whether their values
 * advance, the first and last, and the other callbacks after the feeding stopped (the underrun's
 * report).
 *
 * Runs as a developer-mode native app (tests/regress/smp/pack.sh): the firmware gives it its app
 * role on the bus, which libplayerAPIs' resource manager client needs. Its directory holds
 * clip.h264 (Annex-B with access unit delimiters). The library is loaded at run time and called
 * through its C++ symbols: Feed returns a std::string (old or C++11 ABI by firmware), whose data
 * pointer is its first word either way; on ARM the hidden return pointer goes first, then this. */
#define _GNU_SOURCE /* RTLD_DEFAULT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <syslog.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <wayland-client.h>

struct sstr {
    const char *p;
    char pad[28];
};
typedef void (*cb_t)(int type, int64_t num, const char *str, void *data);

static void (*ctor)(void *, const char *);
static struct sstr (*feed)(void *, const char *);
static int (*load)(void *, const char *, cb_t, void *);
static int (*play)(void *);
static int (*foreground)(void *);
static int (*eos)(void *);
static int (*unload)(void *);
static const char *(*media_id)(void *);
/* libAcbAPI (AcbAPI.h): absent on webOS 1 and 2, whose media need no ACB client */
typedef void (*acb_cb_t)(long acb, long task, long event, long app, long play, const char *reply);
static long (*acb_create)(void);
static int (*acb_initialize)(long, int, const char *, acb_cb_t);
static int (*acb_media_id)(long, const char *);
static int (*acb_sink_type)(long, int);
static int (*acb_state)(long, int, int, long *);
static int (*acb_window)(long, long, long, long, long, int, long *);
static int (*acb_video_data)(long, const char *, long *);
static void (*acb_destroy)(long);
static long acb;
static void *api;
#define ACB_MSE        10 /* PLAYER_TYPE_MSE, as ss4s */
#define ACB_FOREGROUND 1
#define ACB_UNLOADED   0
#define ACB_LOADED     1
#define ACB_PLAYING    2
#define VIDEO_INFO     4 /* STARFISH_EVENT_STR_VIDEO_INFO: to ACB's setMediaVideoData */

#define FPS           30
#define FRAMEREADY    0
#define LOADCOMPLETED 0x16

static int64_t t0;
static volatile int loaded, feeding, ended;
static int ready_fed, ready_back, after;
static int64_t ready_first = -1, ready_last = -1;

/* PmLog (libPmLogLib, which libplayerAPIs brings): a plain syslog line from the app's jail does
 * not reach /var/log/messages */
static int (*pmlog)(void *ctx, int level, const char *msgid, const char *kv, const char *text);
static void *pmctx;

static void say(const char *fmt, ...)
{
    va_list ap;
    char line[512];
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (pmlog)
        pmlog(pmctx, 4 /* warning: kept at every context level */, "SMPCHECK", NULL, line);
    /* and to syslog, legacy-log: webOS 5's libPmLogLib drops what its whitelist does not name */
    syslog(LOG_WARNING, "%s%s", strncmp(line, "SMPCHECK", 8) ? "SMPCHECK " : "", line);
    puts(line);
    fflush(stdout);
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void callback(int type, int64_t num, const char *str, void *data)
{
    (void)data;
    say("%lld cb type=%d num=%lld str=%.300s", (long long)(t0 ? now_ms() - t0 : -1), type,
        (long long)num, str ? str : "");
    if (type == VIDEO_INFO && acb && str)
        acb_video_data(acb, str, NULL);
    if (type == LOADCOMPLETED && !loaded) {
        long task;
        loaded = 1;
        if (acb) {
            acb_sink_type(acb, 0 /* main */);
            acb_state(acb, ACB_FOREGROUND, ACB_LOADED, &task);
        }
        say("SMPCHECK play %d", play(api));
        if (acb)
            acb_state(acb, ACB_FOREGROUND, ACB_PLAYING, &task);
    }
    if (type == FRAMEREADY && t0 && !ended) {
        if (ready_first < 0)
            ready_first = num;
        else if (num < ready_last)
            ready_back++;
        ready_last = num;
        ready_fed++;
    } else if (type != FRAMEREADY && t0 && !feeding) {
        after++;
    }
}

/* The app's window: transparent, the screen's size. The webOS shell's interfaces are the
 * firmware's own (libwayland-webos-client), whose get_shell_surface request moved (webOS 3: after
 * get_system_pip; 4: first), so its opcode is looked up by name. */
static struct wl_display *disp;
static struct wl_compositor *compositor;
static struct wl_shell *shell;
static struct wl_shm *shm;
static struct wl_proxy *webos_shell;
static const struct wl_interface *webos_shell_if, *webos_surface_if;
/* webOS 5+ (jhericurl, kisscurl): no ACB; the video plane is an exported window of the app's
 * surface (wl_webos_foreign/wl_webos_exported) and Load is told its id as option.windowId. */
static struct wl_proxy *webos_foreign, *exported;
static const struct wl_interface *webos_foreign_if, *webos_exported_if;
static char window_id[64];

static void global(void *data, struct wl_registry *reg, uint32_t name, const char *iface,
                   uint32_t ver)
{
    (void)data;
    (void)ver;
    if (!strcmp(iface, "wl_compositor"))
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 1);
    else if (!strcmp(iface, "wl_shell"))
        shell = wl_registry_bind(reg, name, &wl_shell_interface, 1);
    else if (!strcmp(iface, "wl_shm"))
        shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (webos_shell_if && !strcmp(iface, "wl_webos_shell"))
        webos_shell = wl_registry_bind(reg, name, webos_shell_if, 1);
    else if (webos_foreign_if && !strcmp(iface, "wl_webos_foreign"))
        webos_foreign = wl_registry_bind(reg, name, webos_foreign_if, 1);
}

static void global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
    (void)data;
    (void)reg;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {global, global_remove};

static void ping(void *data, struct wl_shell_surface *ss, uint32_t serial)
{
    (void)data;
    wl_shell_surface_pong(ss, serial);
}

static void configure(void *data, struct wl_shell_surface *ss, uint32_t edges, int32_t w, int32_t h)
{
    (void)data;
    (void)ss;
    (void)edges;
    (void)w;
    (void)h;
}

static void popup_done(void *data, struct wl_shell_surface *ss)
{
    (void)data;
    (void)ss;
}

static const struct wl_shell_surface_listener shell_surface_listener = {ping, configure,
                                                                        popup_done};

/* wl_webos_exported.window_id_assigned: the compositor names the exported region (version 1,
 * protocol webos-foreign.xml; the listener struct is its single event). */
static void window_id_assigned(void *data, struct wl_proxy *exp, const char *id, uint32_t type)
{
    (void)data;
    (void)exp;
    (void)type;
    snprintf(window_id, sizeof(window_id), "%s", id ? id : "");
}

static const struct {
    void (*window_id_assigned)(void *, struct wl_proxy *, const char *, uint32_t);
} exported_listener = {window_id_assigned};

static void *dispatch(void *display)
{
    while (wl_display_dispatch(display) >= 0)
        ;
    return NULL;
}

static int request(const struct wl_interface *iface, const char *name)
{
    for (int i = 0; i < iface->method_count; i++)
        if (!strcmp(iface->methods[i].name, name))
            return i;
    return -1;
}

static int window(const char *appid, int w, int h)
{
    void *lib = dlopen("libwayland-webos-client.so.1", RTLD_NOW);
    if (lib) {
        webos_shell_if = dlsym(lib, "wl_webos_shell_interface");
        webos_surface_if = dlsym(lib, "wl_webos_shell_surface_interface");
        webos_foreign_if = dlsym(lib, "wl_webos_foreign_interface");
        webos_exported_if = dlsym(lib, "wl_webos_exported_interface");
    }
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        say("SMPCHECK window: no Wayland display");
        return -1;
    }
    struct wl_registry *reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shell || !shm) {
        say("SMPCHECK window: compositor %p shell %p shm %p", (void *)compositor, (void *)shell,
            (void *)shm);
        return -1;
    }
    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    struct wl_shell_surface *ss = wl_shell_get_shell_surface(shell, surface);
    wl_shell_surface_add_listener(ss, &shell_surface_listener, NULL);
    wl_shell_surface_set_class(ss, appid);
    wl_shell_surface_set_toplevel(ss);
    int get = webos_shell ? request(webos_shell_if, "get_shell_surface") : -1;
    int prop = webos_surface_if ? request(webos_surface_if, "set_property") : -1;
    if (get >= 0 && prop >= 0) {
        struct wl_proxy *ws =
            wl_proxy_marshal_constructor(webos_shell, get, webos_surface_if, NULL, surface);
        wl_proxy_marshal(ws, prop, "appId", appid);
        wl_proxy_marshal(ws, prop, "title", "SMP Check");
    }
    /* a transparent ARGB buffer: the video plane below shows through */
    int stride = w * 4, size = stride * h;
    char path[] = "/tmp/smpcheck-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0 || ftruncate(fd, size)) {
        say("SMPCHECK window: no buffer file");
        return -1;
    }
    unlink(path);
    void *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels != MAP_FAILED)
        memset(pixels, 0, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buf =
        wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage(surface, 0, 0, w, h);
    wl_surface_commit(surface);
    wl_display_roundtrip(display);
    disp = display;
    /* webOS 5+: export a video region of this surface and take the compositor's id for it.
     * wl_webos_foreign.export_element(wl_webos_exported, wl_surface, type=0 video_object), then
     * wait for its window_id_assigned event (protocol webos-foreign.xml, both version 1). The
     * id goes into Load's option.windowId. webOS 3/4 announce no wl_webos_foreign and keep ACB. */
    if (webos_foreign && webos_exported_if) {
        int ex = request(webos_foreign_if, "export_element");
        if (ex >= 0) {
            exported = wl_proxy_marshal_constructor(webos_foreign, ex, webos_exported_if, NULL,
                                                    surface, 0u);
            wl_proxy_add_listener(exported, (void (**)(void))&exported_listener, NULL);
            for (int i = 0; i < 200 && !window_id[0]; i++)
                wl_display_roundtrip(display);
        }
    }
    pthread_t t;
    pthread_create(&t, NULL, dispatch, display);
    say("SMPCHECK window %dx%d (webOS shell surface %s, exported %s)", w, h,
        get >= 0 && prop >= 0 ? "yes" : "no", window_id[0] ? window_id : "no");
    return 0;
}

static void acb_callback(long id, long task, long event, long app, long play_state,
                         const char *reply)
{
    say("acb %ld task %ld event %ld state %ld/%ld %.200s", id, task, event, app, play_state,
        reply ? reply : "");
}

#define SYM(v, ...)                                                                                \
    do {                                                                                           \
        const char *n_[] = {__VA_ARGS__, NULL};                                                    \
        for (int i_ = 0; n_[i_] && !v; i_++)                                                       \
            *(void **)&v = dlsym(h, n_[i_]);                                                       \
        if (!v) {                                                                                  \
            say("SMPCHECK no symbol %s", n_[0]);                                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(int argc, char **argv)
{
    char dir[PATH_MAX], path[PATH_MAX + 16];
    ssize_t n = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
    openlog("SMPCHECK", 0, LOG_USER);
    if (n <= 0)
        return 1;
    dir[n] = 0;
    snprintf(path, sizeof(path), "%s/clip.h264", dirname(dir));
    (void)argc;
    (void)argv; /* sam's launch params */
    FILE *f = fopen(path, "rb");
    if (!f) {
        say("SMPCHECK no clip %s", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *clip = malloc(len);
    if (!clip || fread(clip, 1, len, f) != (size_t)len)
        return 1;
    fclose(f);

    void *h = dlopen("libplayerAPIs.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        say("SMPCHECK no library: %s", dlerror());
        return 1;
    }
    int (*getctx)(const char *, void **) =
        (int (*)(const char *, void **))dlsym(RTLD_DEFAULT, "PmLogGetContext");
    *(void **)&pmlog = dlsym(RTLD_DEFAULT, "PmLogString_");
    if (!getctx || getctx("smpcheck", &pmctx))
        pmlog = NULL;
    SYM(ctor, "_ZN17StarfishMediaAPIsC1EPKc");
    SYM(feed, "_ZN17StarfishMediaAPIs4FeedEPKc", "_ZN17StarfishMediaAPIs4FeedB5cxx11EPKc");
    SYM(load, "_ZN17StarfishMediaAPIs4LoadEPKcPFvixS1_PvES2_");
    SYM(play, "_ZN17StarfishMediaAPIs4PlayEv");
    SYM(eos, "_ZN17StarfishMediaAPIs7pushEOSEv");
    SYM(foreground, "_ZN17StarfishMediaAPIs16notifyForegroundEv");
    SYM(unload, "_ZN17StarfishMediaAPIs6UnloadEv");
    SYM(media_id, "_ZN17StarfishMediaAPIs10getMediaIDEv");
    const char *appid = getenv("APPID") ? getenv("APPID") : "com.example.smpcheck";
    window(appid, 1920, 1080);
    sleep(1); /* sam and ACB learn the app is in front */
    void *a = dlopen("libAcbAPI.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (a) {
        *(void **)&acb_create = dlsym(a, "AcbAPI_create");
        *(void **)&acb_initialize = dlsym(a, "AcbAPI_initialize");
        *(void **)&acb_media_id = dlsym(a, "AcbAPI_setMediaId");
        *(void **)&acb_sink_type = dlsym(a, "AcbAPI_setSinkType");
        *(void **)&acb_state = dlsym(a, "AcbAPI_setState");
        *(void **)&acb_window = dlsym(a, "AcbAPI_setDisplayWindow");
        *(void **)&acb_video_data = dlsym(a, "AcbAPI_setMediaVideoData");
        *(void **)&acb_destroy = dlsym(a, "AcbAPI_destroy");
        if (acb_create && acb_initialize && acb_media_id && acb_sink_type && acb_state &&
            acb_window && acb_video_data && acb_destroy) {
            acb = acb_create();
            if (acb && !acb_initialize(acb, ACB_MSE, appid, acb_callback)) {
                acb_destroy(acb);
                acb = 0;
            }
        }
    }
    say("SMPCHECK acb %ld (%s)", acb, a ? "libAcbAPI" : "no libAcbAPI.so.1");

    api = calloc(1, 8192); /* ss4s's header: 76 + a pointer + 4 KiB of room */
    ctor(api, NULL);
    /* as ss4s does before Load: uMediaServer's resource manager serves foreground pipelines */
    say("SMPCHECK foreground %d", foreground(api));
    if (acb)
        acb_media_id(acb, media_id(api));
    char payload[2048], wopt[96] = "";
    if (window_id[0])
        snprintf(wopt, sizeof(wopt), "\"windowId\":\"%s\",", window_id);
    snprintf(payload, sizeof(payload),
             "{\"args\":[{\"mediaTransportType\":\"BUFFERSTREAM\",\"option\":{\"appId\":\"%s\",%s"
             "\"externalStreamingInfo\":{\"contents\":{\"codec\":{\"video\":\"H264\"},"
             "\"esInfo\":{\"pauseAtDecodeTime\":true,\"ptsToDecode\":0,\"seperatedPTS\":true},"
             "\"format\":\"RAW\",\"provider\":\"Chrome\"},\"streamQualityInfo\":true,"
             "\"audioSync\":true,\"restartStreaming\":false},"
             "\"transmission\":{\"contentsType\":\"WEBRTC\"},\"needAudio\":false,"
             "\"queryPosition\":false,\"lowDelayMode\":true,"
             "\"adaptiveStreaming\":{\"audioOnly\":false,\"maxWidth\":1280,\"maxHeight\":720,"
             "\"maxFrameRate\":%d}}}]}",
             appid, wopt, FPS);
    say("SMPCHECK load %d", load(api, payload, callback, NULL));
    /* webOS 5+: place the exported region: source the coded size, destination the whole screen
     * (as ss4s's StarfishResourcePostLoad: SDL_webOSSetExportedWindow; wl_webos_exported.
     * set_exported_window takes a wl_region pair). */
    if (exported) {
        struct wl_region *src = wl_compositor_create_region(compositor);
        struct wl_region *dst = wl_compositor_create_region(compositor);
        int se = request(webos_exported_if, "set_exported_window");
        wl_region_add(src, 0, 0, 1280, 720);
        wl_region_add(dst, 0, 0, 1920, 1080);
        if (se >= 0)
            wl_proxy_marshal(exported, se, src, dst);
        wl_region_destroy(src);
        wl_region_destroy(dst);
        if (disp)
            wl_display_flush(disp);
    }
    long task;
    if (acb)
        acb_window(acb, 0, 0, 1280, 720, 1 /* full screen */, &task);

    static const unsigned char aud[] = {0, 0, 0, 1, 9};
    static long starts[4096];
    int count = 0;
    for (long i = 0; i + 5 <= len && count < 4095; i++)
        if (!memcmp(clip + i, aud, 5))
            starts[count++] = i;
    starts[count] = len;
    t0 = now_ms();
    feeding = 1;
    for (int i = 0; i < count; i++) {
        int64_t due = t0 + i * 1000LL / FPS;
        while (now_ms() < due)
            usleep(2000);
        unsigned long long pts = i * 1000000000ULL / FPS;
        snprintf(payload, sizeof(payload),
                 "{\"bufferAddr\":\"%p\",\"bufferSize\":%u,\"pts\":%llu,\"esData\":1}",
                 (void *)(clip + starts[i]), (unsigned)(starts[i + 1] - starts[i]), pts);
        for (int tries = 0;; tries++) {
            struct sstr r = feed(api, payload);
            if (r.p && strstr(r.p, "Ok"))
                break;
            if (tries % 50 == 0)
                say("%lld feed %d: %s", (long long)(now_ms() - t0), i, r.p ? r.p : "?");
            usleep(10000);
        }
        if (i % FPS == 0)
            say("%lld fed %d pts %llu ms", (long long)(now_ms() - t0), i, pts / 1000000);
    }
    int64_t fed_ms = now_ms() - t0;
    feeding = 0;
    say("%lld starving", (long long)fed_ms);
    sleep(3);
    ended = 1;
    say("%lld eos %d", (long long)(now_ms() - t0), eos(api));
    sleep(2);
    say("%lld unload %d", (long long)(now_ms() - t0), unload(api));
    if (acb) {
        acb_state(acb, ACB_FOREGROUND, ACB_UNLOADED, &task);
        acb_destroy(acb);
    }
    /* webOS 5+: destroy the exported region  */
    if (exported) {
        int de = request(webos_exported_if, "destroy");
        if (de >= 0)
            wl_proxy_marshal(exported, de);
        wl_proxy_destroy(exported);
        exported = NULL;
    }
    sleep(1);
    say("SMPCHECK frames=%d fed_ms=%lld frameready=%d first=%lld last=%lld backwards=%d "
        "after_feed=%d",
        count, (long long)fed_ms, ready_fed, (long long)ready_first, (long long)ready_last,
        ready_back, after);
    return 0;
}
