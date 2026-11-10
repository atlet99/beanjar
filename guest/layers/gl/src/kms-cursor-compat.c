// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Transport the actual KMS cursor plane over the emulator's cursor port.
 * QEMU's native virgl cursor is fixed64x64, while stock libim uses variable BOs. */
#define _GNU_SOURCE
#include "../../../../include/webos-cursor.h"
#include <dlfcn.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <glob.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xf86drmMode.h>
#ifndef CURSOR_PORT
#define CURSOR_PORT "/dev/virtio-ports/webos.cursor"
#endif
struct tracked {
    struct gbm_bo *bo;
    int fd;
    uint32_t handle;
};
static struct tracked objects[256];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
struct packet {
    struct webos_cursor_header h;
    unsigned char pixels[WEBOS_CURSOR_MAX * WEBOS_CURSOR_MAX * 4];
};
static struct packet state;
static struct gbm_bo *active_bo;
static void *symbol(const char *library, const char *name)
{
    void *p = dlsym(RTLD_NEXT, name);
    if (!p) {
        void *h = dlopen(library, RTLD_NOLOAD | RTLD_NOW);
        if (h) {
            p = dlsym(h, name);
            /* The caller already owns the loaded library; release our lookup reference. */
            dlclose(h);
        }
    }
    return p;
}
static int port(void)
{
    int fd = open(CURSOR_PORT, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0)
        return fd;
#ifndef CURSOR_TEST
    glob_t paths = {0};
    if (!glob("/sys/class/virtio-ports/*/name", 0, NULL, &paths)) {
        for (size_t i = 0; i < paths.gl_pathc; i++) {
            char name[64], leaf[64], path[128];
            FILE *f = fopen(paths.gl_pathv[i], "r");
            if (!f)
                continue;
            int match = fgets(name, sizeof name, f) && !strcmp(name, "webos.cursor\n");
            fclose(f);
            if (match &&
                sscanf(paths.gl_pathv[i], "/sys/class/virtio-ports/%63[^/]/name", leaf) == 1) {
                snprintf(path, sizeof path, "/dev/%s", leaf);
                fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
                break;
            }
        }
    }
    globfree(&paths);
#endif
    return fd;
}
static pthread_t thread;
static int running, started;
static unsigned generation;
static void *sender(void *unused)
{
    (void)unused;
    struct packet pending;
    unsigned seen = 0, ticks = 25;
    int fd = -1;
    size_t offset = 0, length = 0;
    for (;;) {
        pthread_mutex_lock(&lock);
        int live = running;
        if (offset == length && state.h.width && (seen != generation || ticks >= 25)) {
            pending = state;
            seen = generation;
            offset = 0;
            length = sizeof pending.h + (size_t)pending.h.width * pending.h.height * 4;
            pending.h.bytes = htole32(length);
            ticks = 0;
        }
        pthread_mutex_unlock(&lock);
        if (!live)
            break;
        if (fd < 0)
            fd = port();
        if (fd >= 0 && offset < length) {
            ssize_t n = write(fd, (char *)&pending + offset, length - offset);
            if (n > 0)
                offset += (size_t)n;
            else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                close(fd);
                fd = -1;
                offset = length = 0;
                ticks = 25;
            }
        }
        usleep(10000);
        ticks++;
    }
    if (fd >= 0)
        close(fd);
    return NULL;
}
static int changed(void)
{
    if (!started) {
        memcpy(state.h.magic, WEBOS_CURSOR_MAGIC, 8);
        running = 1;
        int err = pthread_create(&thread, NULL, sender, NULL);
        if (err) {
            running = 0;
            errno = err;
            return -1;
        }
        started = 1;
    }
    generation++;
    return 0;
}
__attribute__((destructor)) static void finish(void)
{
    pthread_mutex_lock(&lock);
    running = 0;
    int join = started;
    pthread_mutex_unlock(&lock);
    if (join)
        pthread_join(thread, NULL);
}
struct gbm_bo *gbm_bo_create(struct gbm_device *dev, uint32_t w, uint32_t h, uint32_t format,
                             uint32_t flags)
{
    struct gbm_bo *(*next)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t) =
        symbol("libgbm.so.1", "gbm_bo_create");
    if (!next) {
        errno = ENOSYS;
        return NULL;
    }
    struct gbm_bo *bo = next(dev, w, h, format, flags);
    int saved = errno;
    if (bo && (flags & GBM_BO_USE_CURSOR)) {
        union gbm_bo_handle (*handle)(struct gbm_bo *) = symbol("libgbm.so.1", "gbm_bo_get_handle");
        int (*get_fd)(struct gbm_device *) = symbol("libgbm.so.1", "gbm_device_get_fd");
        if (handle && get_fd) {
            struct tracked t = {bo, get_fd(dev), handle(bo).u32};
            pthread_mutex_lock(&lock);
            for (unsigned i = 0; i < 256; i++)
                if (!objects[i].bo) {
                    objects[i] = t;
                    break;
                }
            pthread_mutex_unlock(&lock);
        }
    }
    errno = saved;
    return bo;
}
void gbm_bo_destroy(struct gbm_bo *bo)
{
    void (*next)(struct gbm_bo *) = symbol("libgbm.so.1", "gbm_bo_destroy");
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < 256; i++)
        if (objects[i].bo == bo)
            objects[i] = (struct tracked){0};
    if (active_bo == bo) {
        state.h.visible = 0;
        (void)changed();
        active_bo = NULL;
    }
    pthread_mutex_unlock(&lock);
    if (next)
        next(bo);
}
static int set_cursor(int fd, uint32_t crtc, uint32_t handle, uint32_t w, uint32_t h, int32_t hx,
                      int32_t hy)
{
    pthread_mutex_lock(&lock);
    if (!handle) {
        state.h.visible = 0;
        active_bo = NULL;
        int result = changed();
        pthread_mutex_unlock(&lock);
        return result;
    }
    if (!w || !h || w > WEBOS_CURSOR_MAX || h > WEBOS_CURSOR_MAX || hx < 0 || hy < 0 ||
        hx >= (int32_t)w || hy >= (int32_t)h) {
        pthread_mutex_unlock(&lock);
        errno = EINVAL;
        return -1;
    }
    struct gbm_bo *bo = NULL;
    for (unsigned i = 0; i < 256; i++)
        if (objects[i].fd == fd && objects[i].handle == handle) {
            bo = objects[i].bo;
            break;
        }
    uint32_t (*format)(struct gbm_bo *) = symbol("libgbm.so.1", "gbm_bo_get_format");
    uint32_t (*width)(struct gbm_bo *) = symbol("libgbm.so.1", "gbm_bo_get_width");
    uint32_t (*height)(struct gbm_bo *) = symbol("libgbm.so.1", "gbm_bo_get_height");
    void *(*map)(struct gbm_bo *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *,
                 void **) = symbol("libgbm.so.1", "gbm_bo_map");
    void (*unmap)(struct gbm_bo *, void *) = symbol("libgbm.so.1", "gbm_bo_unmap");
    drmModeCrtc *(*get_crtc)(int, uint32_t) = symbol("libdrm.so.2", "drmModeGetCrtc");
    void (*free_crtc)(drmModeCrtc *) = symbol("libdrm.so.2", "drmModeFreeCrtc");
    if (!bo || !format || !width || !height || !map || !unmap || !get_crtc || !free_crtc) {
        pthread_mutex_unlock(&lock);
        errno = ENOENT;
        return -1;
    }
    if (format(bo) != GBM_FORMAT_ARGB8888 || width(bo) != w || height(bo) != h) {
        pthread_mutex_unlock(&lock);
        errno = EINVAL;
        return -1;
    }
    drmModeCrtc *c = get_crtc(fd, crtc);
    if (!c || !c->width || !c->height) {
        if (c)
            free_crtc(c);
        pthread_mutex_unlock(&lock);
        errno = EINVAL;
        return -1;
    }
    uint32_t sw = c->width, sh = c->height;
    free_crtc(c);
    uint32_t stride = 0;
    void *data = NULL;
    void *pixels = map(bo, 0, 0, w, h, GBM_BO_TRANSFER_READ, &stride, &data);
    if (!pixels) {
        int saved = errno;
        pthread_mutex_unlock(&lock);
        errno = saved;
        return -1;
    }
    if (stride < w * 4) {
        unmap(bo, data);
        pthread_mutex_unlock(&lock);
        errno = EINVAL;
        return -1;
    }
    for (uint32_t y = 0; y < h; y++)
        memcpy(state.pixels + (size_t)y * w * 4, (char *)pixels + (size_t)y * stride, w * 4);
    unmap(bo, data);
    state.h.width = w;
    state.h.height = h;
    state.h.draw_w = w;
    state.h.draw_h = h;
    state.h.hot_x = hx;
    state.h.hot_y = hy;
    state.h.screen_w = sw;
    state.h.screen_h = sh;
    state.h.visible = 1;
    active_bo = bo;
    int result = changed();
    pthread_mutex_unlock(&lock);
    return result;
}
int drmModeSetCursor(int fd, uint32_t crtc, uint32_t handle, uint32_t w, uint32_t h)
{
    return set_cursor(fd, crtc, handle, w, h, 0, 0);
}
int drmModeSetCursor2(int fd, uint32_t crtc, uint32_t handle, uint32_t w, uint32_t h, int32_t hx,
                      int32_t hy)
{
    return set_cursor(fd, crtc, handle, w, h, hx, hy);
}
int drmModeMoveCursor(int fd, uint32_t crtc, int x, int y)
{
    (void)fd;
    (void)crtc;
    pthread_mutex_lock(&lock);
    state.h.x = x;
    state.h.y = y;
    int result = changed();
    pthread_mutex_unlock(&lock);
    return result;
}
