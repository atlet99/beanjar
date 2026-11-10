// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* splash: beanjar's boot splash on virtio-gpu, until the firmware's compositor takes the display
 *. Without it every frontend shows QEMU's "Display output is not active." until the
 * compositor's first modeset.
 *
 *   splash [IMAGE]     (default /etc/splash.ppm)
 *
 * IMAGE is a binary PPM made at build time (meson/splash.py from host/ui/data/icon.svg): the logo
 * and the name over the background, laid out for a 1080-line screen. Its top-left pixel is the
 * background. It is drawn centred, scaled to the mode the connector offers.
 *
 * The foreground part is bounded and runs before /init starts anything of the firmware: open the
 * DRM device (and so become its master), draw, scan out, then drop master. No compositor can race
 * that, and the compositor that opens the device later becomes master as it does without us.
 * fbdev emulation's /dev/fb0 is no use here: its modeset (FBIOPAN_DISPLAY) shows a 2D resource,
 * whose pixels do not reach a GLES host (the Pi's V3D) - black. So, like gop-present.c, a virgl
 * RGBA resource where the host has virgl, else a dumb buffer.
 *
 * Then it forks: the child keeps the device open (closing it would remove the framebuffer and
 * turn the output off: the placeholder again) and checks once a second whether some CRTC shows
 * a framebuffer that is not ours. Then the compositor has the display and the child exits. If
 * none ever comes (a boot that fails before its compositor), the splash stays.
 *
 * Any failure exits quietly, with a line on stderr (/run/splash.log). */
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/virtgpu_drm.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DRM_NODE     "/dev/dri/card0"
#define DESIGN_LINES 1080 /* the screen height IMAGE is laid out for */

#define VIRGL_FORMAT_R8G8B8A8_UNORM 67
#define VIRGL_BIND_RENDER_TARGET    (1 << 1)
#define VIRGL_BIND_SCANOUT          (1 << 18)
#define PIPE_TEXTURE_2D             2

static int drm, virgl;
static uint32_t crtc_id, conn_id, fb_id, bo_handle, scr_w, scr_h, scr_pitch;
static struct drm_mode_modeinfo mode;
static uint32_t crtcs[16], ncrtcs;

static int fail(const char *what)
{
    fprintf(stderr, "splash: %s: %s\n", what, strerror(errno));
    return 1;
}

/* IMAGE: "P6 <w> <h> 255" and w*h RGB triplets */
static uint8_t *load(const char *path, unsigned *w, unsigned *h)
{
    FILE *f = fopen(path, "rb");
    unsigned max;
    if (!f)
        return NULL;
    uint8_t *px = NULL;
    if (fscanf(f, "P6 %u %u %u", w, h, &max) == 3 && max == 255 && fgetc(f) != EOF && *w && *h &&
        *w <= 4096 && *h <= 4096) {
        size_t n = (size_t)*w * *h * 3;
        px = malloc(n);
        if (px && fread(px, 1, n, f) != n) {
            free(px);
            px = NULL;
        }
    }
    fclose(f);
    return px;
}

/* the first connected connector, its preferred mode, a CRTC for it */
static int pick_output(void)
{
    struct drm_mode_card_res res = {0};
    uint32_t conns[16], encs[16];
    if (ioctl(drm, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        return -1;
    if (res.count_connectors > 16 || res.count_crtcs > 16 || res.count_encoders > 16)
        return -1;
    res.connector_id_ptr = (uintptr_t)conns;
    res.crtc_id_ptr = (uintptr_t)crtcs;
    res.encoder_id_ptr = (uintptr_t)encs;
    res.count_fbs = 0;
    if (ioctl(drm, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        return -1;
    ncrtcs = res.count_crtcs;
    for (unsigned i = 0; i < res.count_connectors && !crtc_id; i++) {
        struct drm_mode_get_connector c = {.connector_id = conns[i]};
        struct drm_mode_modeinfo modes[32];
        uint32_t cencs[8];
        if (ioctl(drm, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.connection != 1 || !c.count_modes)
            continue;
        if (c.count_modes > 32)
            c.count_modes = 32;
        if (c.count_encoders > 8)
            c.count_encoders = 8;
        c.modes_ptr = (uintptr_t)modes;
        c.encoders_ptr = (uintptr_t)cencs;
        c.count_props = 0;
        if (ioctl(drm, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0)
            continue;
        mode = modes[0];
        for (unsigned m = 0; m < c.count_modes; m++)
            if (modes[m].type & DRM_MODE_TYPE_PREFERRED) {
                mode = modes[m];
                break;
            }
        conn_id = c.connector_id;
        for (unsigned e = 0; e < c.count_encoders && !crtc_id; e++) {
            struct drm_mode_get_encoder enc = {.encoder_id = cencs[e]};
            if (ioctl(drm, DRM_IOCTL_MODE_GETENCODER, &enc) < 0)
                continue;
            for (unsigned k = 0; k < res.count_crtcs; k++)
                if (enc.possible_crtcs & (1u << k)) {
                    crtc_id = crtcs[k];
                    break;
                }
        }
    }
    if (!crtc_id) {
        errno = ENODEV;
        return -1;
    }
    scr_w = mode.hdisplay;
    scr_h = mode.vdisplay;
    return 0;
}

/* the scanout buffer, mapped */
static uint32_t *create_buffer(size_t *size)
{
    uint64_t has_3d = 0;
    struct drm_virtgpu_getparam gp = {.param = VIRTGPU_PARAM_3D_FEATURES,
                                      .value = (uintptr_t)&has_3d};
    virgl = ioctl(drm, DRM_IOCTL_VIRTGPU_GETPARAM, &gp) == 0 && has_3d;
    off_t offset;
    if (virgl) {
        struct drm_virtgpu_resource_create rc = {.target = PIPE_TEXTURE_2D,
                                                 .format = VIRGL_FORMAT_R8G8B8A8_UNORM,
                                                 .bind =
                                                     VIRGL_BIND_RENDER_TARGET | VIRGL_BIND_SCANOUT,
                                                 .width = scr_w,
                                                 .height = scr_h,
                                                 .depth = 1,
                                                 .array_size = 1,
                                                 .size = scr_w * scr_h * 4,
                                                 .stride = scr_w * 4};
        if (ioctl(drm, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE, &rc) < 0)
            return NULL;
        bo_handle = rc.bo_handle;
        scr_pitch = scr_w * 4;
        *size = rc.size;
        struct drm_virtgpu_map vm = {.handle = bo_handle};
        if (ioctl(drm, DRM_IOCTL_VIRTGPU_MAP, &vm) < 0)
            return NULL;
        offset = (off_t)vm.offset;
    } else {
        struct drm_mode_create_dumb cd = {.width = scr_w, .height = scr_h, .bpp = 32};
        if (ioctl(drm, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0)
            return NULL;
        bo_handle = cd.handle;
        scr_pitch = cd.pitch;
        *size = cd.size;
        struct drm_mode_map_dumb md = {.handle = bo_handle};
        if (ioctl(drm, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0)
            return NULL;
        offset = (off_t)md.offset;
    }
    /* XRGB8888, virtio-gpu's primary format; with virgl the host reads the resource as RGBA */
    struct drm_mode_fb_cmd fb = {.width = scr_w,
                                 .height = scr_h,
                                 .pitch = scr_pitch,
                                 .bpp = 32,
                                 .depth = 24,
                                 .handle = bo_handle};
    if (ioctl(drm, DRM_IOCTL_MODE_ADDFB, &fb) < 0)
        return NULL;
    fb_id = fb.fb_id;
    void *p = mmap(NULL, *size, PROT_READ | PROT_WRITE, MAP_SHARED, drm, offset);
    return p == MAP_FAILED ? NULL : p;
}

static inline uint32_t pixel(unsigned r, unsigned g, unsigned b)
{
    /* virgl: the bytes R, G, B, A; dumb buffer: XRGB8888 */
    return virgl ? 0xFF000000u | b << 16 | g << 8 | r : 0xFF000000u | r << 16 | g << 8 | b;
}

/* IMAGE centred over its background, scaled by the mode's height to DESIGN_LINES (bilinear) */
static void draw(uint32_t *screen, const uint8_t *img, unsigned iw, unsigned ih)
{
    uint32_t bg = pixel(img[0], img[1], img[2]);
    double s = (double)scr_h / DESIGN_LINES;
    if (iw * s > scr_w)
        s = (double)scr_w / iw;
    unsigned dw = (unsigned)(iw * s + 0.5), dh = (unsigned)(ih * s + 0.5);
    if (dw > scr_w)
        dw = scr_w;
    if (dh > scr_h)
        dh = scr_h;
    unsigned x0 = (scr_w - dw) / 2, y0 = (scr_h - dh) / 2;
    for (unsigned y = 0; y < scr_h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)screen + (size_t)y * scr_pitch);
        if (y < y0 || y >= y0 + dh) {
            for (unsigned x = 0; x < scr_w; x++)
                row[x] = bg;
            continue;
        }
        for (unsigned x = 0; x < x0; x++)
            row[x] = bg;
        for (unsigned x = x0 + dw; x < scr_w; x++)
            row[x] = bg;
        double fy = (y - y0 + 0.5) / s - 0.5;
        if (fy < 0)
            fy = 0;
        unsigned sy = (unsigned)fy, sy1 = sy + 1 < ih ? sy + 1 : sy;
        unsigned wy = (unsigned)((fy - sy) * 256);
        for (unsigned x = 0; x < dw; x++) {
            double fx = (x + 0.5) / s - 0.5;
            if (fx < 0)
                fx = 0;
            unsigned sx = (unsigned)fx, sx1 = sx + 1 < iw ? sx + 1 : sx;
            unsigned wx = (unsigned)((fx - sx) * 256);
            const uint8_t *a = img + ((size_t)sy * iw + sx) * 3,
                          *b = img + ((size_t)sy * iw + sx1) * 3,
                          *c = img + ((size_t)sy1 * iw + sx) * 3,
                          *d = img + ((size_t)sy1 * iw + sx1) * 3;
            unsigned ch[3];
            for (int k = 0; k < 3; k++) {
                unsigned top = a[k] * (256 - wx) + b[k] * wx, bot = c[k] * (256 - wx) + d[k] * wx;
                ch[k] = (top * (256 - wy) + bot * wy + 32768) >> 16;
            }
            row[x0 + x] = pixel(ch[0], ch[1], ch[2]);
        }
    }
}

/* some CRTC shows a framebuffer that is not ours: a compositor has the display */
static int taken_over(void)
{
    for (unsigned k = 0; k < ncrtcs; k++) {
        struct drm_mode_crtc c = {.crtc_id = crtcs[k]};
        if (ioctl(drm, DRM_IOCTL_MODE_GETCRTC, &c) < 0)
            return 1;
        if (c.fb_id && c.fb_id != fb_id)
            return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned iw, ih;
    alarm(10); /* the foreground part's bound: SIGALRM ends it, and /init goes on */
    uint8_t *img = load(argc > 1 ? argv[1] : "/etc/splash.ppm", &iw, &ih);
    if (!img)
        return fail("image");
    drm = open(DRM_NODE, O_RDWR | O_CLOEXEC);
    if (drm < 0)
        return fail(DRM_NODE);
    if (pick_output() < 0)
        return fail("no connected output");
    size_t size;
    uint32_t *screen = create_buffer(&size);
    if (!screen)
        return fail("scanout buffer");
    draw(screen, img, iw, ih);
    free(img);
    if (virgl) {
        struct drm_virtgpu_3d_transfer_to_host t = {.bo_handle = bo_handle,
                                                    .box = {.w = scr_w, .h = scr_h, .d = 1}};
        if (ioctl(drm, DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST, &t) < 0)
            return fail("transfer");
        struct drm_virtgpu_3d_wait w = {.handle = bo_handle};
        ioctl(drm, DRM_IOCTL_VIRTGPU_WAIT, &w);
    }
    munmap(screen, size);
    struct drm_mode_crtc crtc = {.crtc_id = crtc_id,
                                 .fb_id = fb_id,
                                 .set_connectors_ptr = (uintptr_t)&conn_id,
                                 .count_connectors = 1,
                                 .mode = mode,
                                 .mode_valid = 1};
    if (ioctl(drm, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0)
        return fail("SETCRTC");
    struct drm_clip_rect clip = {0, 0, (unsigned short)scr_w, (unsigned short)scr_h};
    struct drm_mode_fb_dirty_cmd dirty = {
        .fb_id = fb_id, .num_clips = 1, .clips_ptr = (uintptr_t)&clip};
    ioctl(drm, DRM_IOCTL_MODE_DIRTYFB, &dirty);
    /* the compositor that opens the device next becomes its master, as without us */
    if (ioctl(drm, DRM_IOCTL_DROP_MASTER, 0) < 0)
        return fail("DROP_MASTER");
    fprintf(stderr, "splash: %ux%u on CRTC %u, connector %u (%s); master dropped\n", scr_w, scr_h,
            crtc_id, conn_id, virgl ? "virgl resource" : "dumb buffer");
    alarm(0);
    pid_t pid = fork();
    if (pid < 0)
        return fail("fork");
    if (pid > 0)
        return 0;
    setsid();
    struct timespec second = {1, 0};
    while (!taken_over())
        nanosleep(&second, NULL);
    fprintf(stderr, "splash: the display is taken over; exiting\n");
    return 0;
}
