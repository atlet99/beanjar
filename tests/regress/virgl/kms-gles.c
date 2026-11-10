// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* KMS/GBM/GLES2 scanout test for virtio-gpu (virgl) in the guest.
 *
 *   kms-gles [argb|xrgb] [SECONDS]
 *
 * Renders with GLES2 into a GBM surface on /dev/dri/card0, puts it on the first connected
 * connector with drmModeSetCrtc and page-flips for SECONDS (default 20) so the host can take a
 * screendump. The GBM surface format is ARGB8888 (default) or XRGB8888; the framebuffer is
 * added as that format, falling back to XRGB8888 when the primary plane refuses ARGB8888.
 *
 * Picture (no blending, so the stored alpha is exactly what is written):
 *   background     dark grey  (64,64,64)    alpha 255
 *   left third     red        (255,0,0)     alpha 255
 *   middle third   green      (0,255,0)     alpha 128 (half transparent)
 *   right third    blue       (0,0,255)     alpha 0   (fully transparent)
 *   a white bar moving across the top, one step per flip
 * It prints the primary plane's formats, the formats used, and a readback of each region. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void die(const char *m)
{
    fprintf(stderr, "FAIL: %s (errno %s, egl 0x%x)\n", m, strerror(errno), eglGetError());
    exit(1);
}
static const char *fcc(uint32_t f, char *b)
{
    memcpy(b, &f, 4);
    b[4] = 0;
    return b;
}

static int flip_pending;
static void on_flip(int fd, unsigned seq, unsigned s, unsigned us, void *data)
{
    flip_pending = 0;
}

static uint32_t add_fb(int fd, struct gbm_bo *bo, uint32_t format)
{
    uint32_t handles[4] = {gbm_bo_get_handle(bo).u32}, pitches[4] = {gbm_bo_get_stride(bo)},
             offsets[4] = {0}, id;
    if (drmModeAddFB2(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), format, handles, pitches,
                      offsets, &id, 0))
        return 0;
    return id;
}

int main(int argc, char **argv)
{
    int want_argb = !(argc > 1 && !strcmp(argv[1], "xrgb"));
    int seconds = argc > 2 ? atoi(argv[2]) : 20;
    char b1[5], b2[5];

    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        die("open card0");
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmModeRes *res = drmModeGetResources(fd);
    if (!res)
        die("drmModeGetResources");
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes)
            conn = c;
        else
            drmModeFreeConnector(c);
    }
    if (!conn)
        die("no connected connector");
    drmModeModeInfo mode = conn->modes[0];
    for (int i = 0; i < conn->count_modes; i++)
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            mode = conn->modes[i];
            break;
        }
    uint32_t crtc = res->crtcs[0];
    if (conn->encoder_id) {
        drmModeEncoder *e = drmModeGetEncoder(fd, conn->encoder_id);
        if (e && e->crtc_id)
            crtc = e->crtc_id;
    }
    printf("connector %u mode %s %dx%d@%d crtc %u\n", conn->connector_id, mode.name, mode.hdisplay,
           mode.vdisplay, mode.vrefresh, crtc);

    drmModePlaneRes *pr = drmModeGetPlaneResources(fd);
    for (unsigned i = 0; pr && i < pr->count_planes; i++) {
        drmModePlane *p = drmModeGetPlane(fd, pr->planes[i]);
        printf("plane %u formats:", p->plane_id);
        for (unsigned j = 0; j < p->count_formats; j++)
            printf(" %s", fcc(p->formats[j], b1));
        printf("\n");
        drmModeFreePlane(p);
    }

    struct gbm_device *gbm = gbm_create_device(fd);
    if (!gbm)
        die("gbm_create_device");
    uint32_t gbm_format = want_argb ? GBM_FORMAT_ARGB8888 : GBM_FORMAT_XRGB8888;
    struct gbm_surface *gs = gbm_surface_create(gbm, mode.hdisplay, mode.vdisplay, gbm_format,
                                                GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gs)
        die("gbm_surface_create");

    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = gpd(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    if (!eglInitialize(dpy, NULL, NULL))
        die("eglInitialize");
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cattr[] = {EGL_SURFACE_TYPE,
                            EGL_WINDOW_BIT,
                            EGL_RENDERABLE_TYPE,
                            EGL_OPENGL_ES2_BIT,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_ALPHA_SIZE,
                            want_argb ? 8 : 0,
                            EGL_NONE};
    EGLConfig cfgs[64], cfg = NULL;
    EGLint n;
    if (!eglChooseConfig(dpy, cattr, cfgs, 64, &n) || n < 1)
        die("eglChooseConfig");
    for (int i = 0; i < n && !cfg; i++) {
        EGLint vid;
        eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &vid);
        if ((uint32_t)vid == gbm_format)
            cfg = cfgs[i];
    }
    if (!cfg)
        die("no EGL config with the GBM format");
    const EGLint ctxattr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)gs, NULL);
    if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE)
        die("context/surface");
    if (!eglMakeCurrent(dpy, surf, surf, ctx))
        die("eglMakeCurrent");
    printf("GL_RENDERER=%s\nGBM surface %s\n", glGetString(GL_RENDERER), fcc(gbm_format, b1));

    int W = mode.hdisplay, H = mode.vdisplay, frames = 0, bar = 0;
    struct gbm_bo *prev = NULL;
    uint32_t fb_format = gbm_format;
    drmEventContext ev = {.version = 2, .page_flip_handler = on_flip};
    time_t end = time(NULL) + seconds;
    glEnable(GL_SCISSOR_TEST);
    do {
        glScissor(0, 0, W, H);
        glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(W / 12, H / 6, W / 4, H * 2 / 3);
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(W * 5 / 12, H / 6, W / 4, H * 2 / 3);
        glClearColor(0, 1, 0, 128 / 255.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(W * 9 / 12, H / 6, W / 6, H * 2 / 3);
        glClearColor(0, 0, 1, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(bar, H - H / 12, W / 20, H / 24);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        bar = (bar + W / 60) % (W - W / 20);
        if (frames == 0) {
            static const char *names[] = {"background", "red a=255", "green a=128", "blue a=0"};
            int xs[] = {10, W / 6, W / 2, W * 5 / 6};
            for (int i = 0; i < 4; i++) {
                unsigned char c[4];
                glReadPixels(xs[i], H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
                printf("readback %-12s = %3u %3u %3u %3u\n", names[i], c[0], c[1], c[2], c[3]);
            }
        }
        if (!eglSwapBuffers(dpy, surf))
            die("eglSwapBuffers");
        struct gbm_bo *bo = gbm_surface_lock_front_buffer(gs);
        uint32_t fb = (uint32_t)(uintptr_t)gbm_bo_get_user_data(bo);
        if (!fb) {
            fb = add_fb(fd, bo, fb_format);
            if (!fb && fb_format == DRM_FORMAT_ARGB8888) {
                printf(
                    "drmModeAddFB2 ARGB8888 refused (%s); using XRGB8888 on the ARGB8888 buffer\n",
                    strerror(errno));
                fb_format = DRM_FORMAT_XRGB8888;
                fb = add_fb(fd, bo, fb_format);
            }
            if (!fb)
                die("drmModeAddFB2");
            gbm_bo_set_user_data(bo, (void *)(uintptr_t)fb, NULL);
        }
        if (frames == 0) {
            if (drmModeSetCrtc(fd, crtc, fb, 0, 0, &conn->connector_id, 1, &mode)) {
                if (fb_format != DRM_FORMAT_ARGB8888)
                    die("drmModeSetCrtc");
                /* virtio-gpu's primary plane is XRGB8888 only; ARGB8888 passes AddFB2 because the
                 * cursor plane has it, but not SetCrtc. Scan the same buffer out as XRGB8888. */
                printf("drmModeSetCrtc with ARGB8888 refused (%s); retrying as XRGB8888\n",
                       strerror(errno));
                drmModeRmFB(fd, fb);
                fb_format = DRM_FORMAT_XRGB8888;
                if (!(fb = add_fb(fd, bo, fb_format)))
                    die("drmModeAddFB2 XRGB8888");
                gbm_bo_set_user_data(bo, (void *)(uintptr_t)fb, NULL);
                if (drmModeSetCrtc(fd, crtc, fb, 0, 0, &conn->connector_id, 1, &mode))
                    die("drmModeSetCrtc");
            }
            printf("scanout fb %u format %s (buffer %s, modifier 0x%llx)\n", fb, fcc(fb_format, b1),
                   fcc(gbm_bo_get_format(bo), b2), (unsigned long long)gbm_bo_get_modifier(bo));
        } else {
            flip_pending = 1;
            if (drmModePageFlip(fd, crtc, fb, DRM_MODE_PAGE_FLIP_EVENT, NULL))
                die("drmModePageFlip");
            while (flip_pending) {
                struct pollfd p = {.fd = fd, .events = POLLIN};
                if (poll(&p, 1, 1000) <= 0)
                    die("page flip timeout");
                drmHandleEvent(fd, &ev);
            }
        }
        if (prev)
            gbm_surface_release_buffer(gs, prev);
        prev = bo;
        frames++;
        usleep(100000);
    } while (time(NULL) < end);
    printf("flipped %d frames in %d s\nPASS\n", frames, seconds);
    return 0;
}
