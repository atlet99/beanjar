// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Pictures in the GPU, read back: the OSD as it is on screen (the virtio-gpu scanout) when a blit
 * reads the OSD plane's frame buffer, and the compositor's latest picture when one reads its GM
 * window (dile-gal.c); that is how gm's GM_CaptureGraphicScreen, tv.capture's picture of the
 * graphic plane, sees the screen.
 *
 * Both are framebuffers of the compositor's process (egl-kms.c), ARGB with alpha 0 where video
 * shows through: the compositor's own, which egl-kms registers, and the one it scans out. They
 * are read in the guest through DRM: GETCRTC gives the framebuffer on screen, GETFB2 a GEM
 * handle for a framebuffer (the caller needs CAP_SYS_ADMIN; gm is root). With
 * virgl the picture is in a host texture, so VIRTGPU_TRANSFER_FROM_HOST first copies it into the
 * resource's guest pages (stride 0: the kernel refuses one for a resource that is not a host
 * blob), then VIRTGPU_MAP + mmap64 read them (DRM's fake offsets are above 4 GiB). The host only
 * transfers a resource attached to the caller's context, and the kernel attaches at handle
 * creation only if the context exists; with context init offered it makes it lazily, so the
 * context is made first with VIRTGPU_CONTEXT_INIT (no parameters). Without virgl (2D virtio-gpu)
 * the pages are the framebuffer and are read directly. This needs no host process: the viewer
 * may not be running, and nothing new crosses QEMU; on the Pi (KVM, dma-buf scanout) and on WSL
 * (the read-back QEMU) the transfer is virglrenderer's transfer_read, not a display path. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#include "osd-readback.h"

/* Kernel UAPI newer than the SDK's 3.10 headers (drm_mode.h, virtgpu_drm.h of Linux 6.x). */
struct fb2 {
    uint32_t fb_id, width, height, pixel_format, flags;
    uint32_t handles[4], pitches[4], offsets[4];
    uint64_t modifier[4];
};
struct card_res {
    uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
    uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;
    uint32_t min_width, max_width, min_height, max_height;
};
struct mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
    uint8_t mode[68]; /* struct drm_mode_modeinfo */
};
struct vgpu_getparam {
    uint64_t param, value;
};
struct vgpu_box {
    uint32_t x, y, z, w, h, d;
};
struct vgpu_transfer {
    uint32_t bo_handle;
    struct vgpu_box box;
    uint32_t level, offset, stride, layer_stride;
};
struct vgpu_wait {
    uint32_t handle, flags;
};
struct vgpu_map {
    uint64_t offset;
    uint32_t handle, pad;
};
struct gem_close {
    uint32_t handle, pad;
};
struct vgpu_context_init {
    uint32_t num_params, pad;
    uint64_t ctx_set_params;
};

#define IOWR(nr, type)             _IOC(_IOC_READ | _IOC_WRITE, 'd', nr, sizeof(type))
#define DRM_GEM_CLOSE              _IOC(_IOC_WRITE, 'd', 0x09, sizeof(struct gem_close))
#define DRM_MODE_GETRESOURCES      IOWR(0xA0, struct card_res)
#define DRM_MODE_GETCRTC           IOWR(0xA1, struct mode_crtc)
#define DRM_MODE_GETFB2            IOWR(0xCE, struct fb2)
#define VIRTGPU_MAP                IOWR(0x41, struct vgpu_map)
#define VIRTGPU_GETPARAM           IOWR(0x43, struct vgpu_getparam)
#define VIRTGPU_TRANSFER_FROM_HOST IOWR(0x46, struct vgpu_transfer)
#define VIRTGPU_WAIT               IOWR(0x48, struct vgpu_wait)
#define VIRTGPU_CONTEXT_INIT       IOWR(0x4B, struct vgpu_context_init)
#define VIRTGPU_PARAM_3D_FEATURES  1
#define FOURCC(a, b, c, d)                                                                         \
    ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsyslog(LOG_INFO, fmt, ap);
    va_end(ap);
}

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do
        r = ioctl(fd, req, arg);
    while (r < 0 && (errno == EINTR || errno == EAGAIN));
    return r;
}

/* A framebuffer (0: the one on the first active CRTC) as w x h pixels B,G,R,A (ARGB8888 in
 * memory), top row first, tightly packed; NULL if there is nothing to read. */
static uint8_t *read_osd(uint32_t fb_id, int *out_w, int *out_h)
{
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        logf_("gal: capture: /dev/dri/card0: %s", strerror(errno));
        return NULL;
    }
    uint8_t *pixels = NULL;
    uint32_t crtcs[8], handle = 0;
    struct card_res res;
    memset(&res, 0, sizeof res);
    res.crtc_id_ptr = (uintptr_t)crtcs;
    res.count_crtcs = 8;
    if (!fb_id && xioctl(fd, DRM_MODE_GETRESOURCES, &res) < 0) {
        logf_("gal: capture: GETRESOURCES: %s", strerror(errno));
        goto out;
    }
    for (uint32_t i = 0; i < res.count_crtcs && i < 8 && !fb_id; ++i) {
        struct mode_crtc c;
        memset(&c, 0, sizeof c);
        c.crtc_id = crtcs[i];
        if (xioctl(fd, DRM_MODE_GETCRTC, &c) == 0 && c.mode_valid)
            fb_id = c.fb_id;
    }
    if (!fb_id) {
        logf_("gal: capture: nothing on screen");
        goto out;
    }
    struct vgpu_context_init ci = {0, 0, 0};
    xioctl(fd, VIRTGPU_CONTEXT_INIT, &ci); /* refused without context init: handles make it */
    struct fb2 fb;
    memset(&fb, 0, sizeof fb);
    fb.fb_id = fb_id;
    if (xioctl(fd, DRM_MODE_GETFB2, &fb) < 0 || !fb.handles[0]) {
        logf_("gal: capture: GETFB2 %u: %s", fb_id, strerror(errno));
        goto out;
    }
    handle = fb.handles[0];
    uint32_t f = fb.pixel_format;
    int swap_rb = f == FOURCC('A', 'B', '2', '4') || f == FOURCC('X', 'B', '2', '4');
    if (!swap_rb && f != FOURCC('A', 'R', '2', '4') && f != FOURCC('X', 'R', '2', '4')) {
        logf_("gal: capture: framebuffer format %.4s not handled", (char *)&f);
        goto out;
    }
    /* XRGB too keeps its alpha bytes: egl-kms adds the compositor's ARGB buffer as XRGB when the
     * plane lacks ARGB, and the bytes are still the compositor's alpha. */
    uint32_t w = fb.width, h = fb.height, pitch = fb.pitches[0];
    int have3d = 0;
    struct vgpu_getparam gp = {VIRTGPU_PARAM_3D_FEATURES, (uintptr_t)&have3d};
    if (xioctl(fd, VIRTGPU_GETPARAM, &gp) < 0)
        have3d = 0;
    if (have3d) {
        struct vgpu_transfer t = {handle, {0, 0, 0, w, h, 1}, 0, fb.offsets[0], 0, 0};
        struct vgpu_wait wt = {handle, 0};
        if (xioctl(fd, VIRTGPU_TRANSFER_FROM_HOST, &t) < 0 || xioctl(fd, VIRTGPU_WAIT, &wt) < 0) {
            logf_("gal: capture: TRANSFER_FROM_HOST: %s", strerror(errno));
            goto out;
        }
    }
    struct vgpu_map m = {0, handle, 0};
    if (xioctl(fd, VIRTGPU_MAP, &m) < 0) {
        logf_("gal: capture: MAP: %s", strerror(errno));
        goto out;
    }
    size_t len = (size_t)fb.offsets[0] + (size_t)pitch * h;
    uint8_t *map = mmap64(NULL, len, PROT_READ, MAP_SHARED, fd, (off64_t)m.offset);
    if (map == MAP_FAILED) {
        logf_("gal: capture: mmap: %s", strerror(errno));
        goto out;
    }
    pixels = malloc((size_t)w * h * 4);
    if (pixels) {
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t *s = map + fb.offsets[0] + (size_t)pitch * y;
            uint8_t *d = pixels + (size_t)w * 4 * y;
            memcpy(d, s, (size_t)w * 4);
            if (swap_rb)
                for (uint32_t x = 0; x < w; ++x) {
                    uint8_t t = d[4 * x];
                    d[4 * x] = d[4 * x + 2];
                    d[4 * x + 2] = t;
                }
        }
        *out_w = (int)w;
        *out_h = (int)h;
    }
    munmap(map, len);
out:
    if (handle) {
        struct gem_close gc = {handle, 0};
        ioctl(fd, DRM_GEM_CLOSE, &gc);
    }
    close(fd);
    return pixels;
}

int osd_readback_argb(uint32_t fb_id, uint8_t *dst, int dw, int dh)
{
    int sw = 0, sh = 0;
    uint8_t *src = read_osd(fb_id, &sw, &sh);
    if (!src)
        return -1;
    /* area average of the premultiplied B,G,R,A bytes */
    for (int y = 0; y < dh; ++y) {
        int y0 = (int)((int64_t)y * sh / dh), y1 = (int)((int64_t)(y + 1) * sh / dh);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (int x = 0; x < dw; ++x) {
            int x0 = (int)((int64_t)x * sw / dw), x1 = (int)((int64_t)(x + 1) * sw / dw);
            if (x1 <= x0)
                x1 = x0 + 1;
            unsigned sum[4] = {0, 0, 0, 0}, n = (unsigned)((x1 - x0) * (y1 - y0));
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t *p = src + ((size_t)yy * sw + x0) * 4;
                for (int xx = x0; xx < x1; ++xx, p += 4)
                    for (int c = 0; c < 4; ++c)
                        sum[c] += p[c];
            }
            uint8_t *d = dst + ((size_t)y * dw + x) * 4;
            for (int c = 0; c < 4; ++c)
                d[c] = (uint8_t)((sum[c] + n / 2) / n);
        }
    }
    free(src);
    return 0;
}
