// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* guest/layers/soc/src/libdile_vt: what the libdile_vt shim (dile-vt.c) takes from the
 * libhal_lg115x shim and from tvservice's private view of the OS.
 *
 * The shim takes its stock library's place under its soname and NEEDs the stock file, like the
 * libhal_lg115x shim. Its functions act only in a process that also has the libhal_lg115x shim
 * and the emulator's physical memory (guest/layers/common/tvservice's libtvservice-private.so,
 * common/physmem.c), i.e. tvservice (tvwin does not load libdile_vt): there they use that
 * physical memory and the video windows (libhal_lg115x/hal-video.c), found at run time. Elsewhere
 * every call goes to LG's library unchanged. */
#ifndef DILE_COMMON_H
#define DILE_COMMON_H
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

struct webos_video_context;

struct dile_hal {
    unsigned (*alloc)(unsigned size, void **virt);
    unsigned (*shown)(struct webos_video_context *out, unsigned max);
};

static struct dile_hal dile_hal_fns;
static pthread_once_t dile_hal_once = PTHREAD_ONCE_INIT;

static void dile_hal_find(void)
{
    dile_hal_fns.alloc =
        (unsigned (*)(unsigned, void **))dlsym(RTLD_DEFAULT, "tvservice_physmem_alloc");
    dile_hal_fns.shown = (unsigned (*)(struct webos_video_context *, unsigned))dlsym(
        RTLD_DEFAULT, "tvservice_hal_video_shown");
    if (!dile_hal_fns.alloc || !dile_hal_fns.shown)
        dile_hal_fns.alloc = NULL;
}

/* The emulator's HAL, or NULL where LG's DILE stays in charge. */
static const struct dile_hal *dile_hal(void)
{
    pthread_once(&dile_hal_once, dile_hal_find);
    return dile_hal_fns.alloc ? &dile_hal_fns : NULL;
}

static void *dile_stock(const char *name)
{
    void *f = dlsym(RTLD_NEXT, name);
    if (!f)
        syslog(LOG_INFO, "tvservice-dile: stock %s not found", name);
    return f;
}

static void dile_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void dile_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    syslog(LOG_INFO, "tvservice-dile: %s", line);
}

/* The viewer's placeholder for a video window (host/viewer/video.c): 60-pixel squares anchored
 * at the window's top-left corner, the corner square dark blue (31, 31, 102), the others
 * alternating with light grey (217). Pixel at (dx, dy) from the corner, as ARGB8888. */
static inline unsigned dile_pattern_argb(int dx, int dy)
{
    int odd = ((dx >= 0 ? dx / 60 : (dx - 59) / 60) + (dy >= 0 ? dy / 60 : (dy - 59) / 60)) & 1;
    return odd ? 0xFFD9D9D9u : 0xFF1F1F66u;
}

#endif
