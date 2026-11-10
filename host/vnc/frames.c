// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
/* beanjar-vnc's GBM frames (frames.h). */
#include "frames.h"
#include "tilediff.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct frames {
    int drm, count, width, height;
    struct tilediff *diff; /* frames_diff's, made at its first use */
    struct gbm_device *gbm;
    char node[64];
    struct frame *frames;
};

static void frame_fini(struct frame *f)
{
    PFNEGLDESTROYIMAGEKHRPROC destroy_image =
        (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    if (f->fbo)
        glDeleteFramebuffers(1, &f->fbo);
    if (f->tex)
        glDeleteTextures(1, &f->tex);
    if (f->image)
        destroy_image(eglGetCurrentDisplay(), f->image);
    if (f->v.pixels)
        munmap((void *)f->v.pixels, f->v.size);
    if (f->v.fd >= 0)
        close(f->v.fd);
    if (f->view && f->view != f->bo)
        gbm_bo_destroy(f->view);
    if (f->bo)
        gbm_bo_destroy(f->bo);
    memset(f, 0, sizeof *f);
    f->v.fd = -1;
}

/* One buffer: allocated, exported, mapped, imported into EGL as a framebuffer. 0, or -1 with the
 * reason in why. */
static int frame_init(struct frames *p, struct frame *f, int w, int h, const char **why)
{
    int ah = (h + 15) & ~15;
    uint64_t linear = DRM_FORMAT_MOD_LINEAR;
    f->v.fd = -1;
    f->bo = gbm_bo_create_with_modifiers2(p->gbm, (uint32_t)w, (uint32_t)ah, DRM_FORMAT_XRGB8888,
                                          &linear, 1, GBM_BO_USE_RENDERING);
    if (!f->bo)
        f->bo = gbm_bo_create(p->gbm, (uint32_t)w, (uint32_t)ah, DRM_FORMAT_XRGB8888,
                              GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!f->bo) {
        *why = "no linear XRGB8888 buffer object";
        return -1;
    }
    if (gbm_bo_get_modifier(f->bo) != DRM_FORMAT_MOD_LINEAR &&
        gbm_bo_get_modifier(f->bo) != DRM_FORMAT_MOD_INVALID) {
        *why = "the buffer object is not linear";
        return -1;
    }
    f->v.fd = gbm_bo_get_fd(f->bo);
    f->v.stride = gbm_bo_get_stride(f->bo);
    if (f->v.fd < 0 || gbm_bo_get_offset(f->bo, 0) != 0) {
        *why = "no dma-buf for the buffer object";
        return -1;
    }
    f->v.fourcc = DRM_FORMAT_XRGB8888;
    f->v.width = w;
    f->v.height = h;
    /* the dma-buf's own size, which may be larger than its rows (panfrost rounds it up) */
    f->v.size = (size_t)f->v.stride * (size_t)ah;
    off_t end = lseek(f->v.fd, 0, SEEK_END);
    if (end > 0 && (size_t)end < f->v.size) {
        *why = "the dma-buf is smaller than its rows";
        return -1;
    }
    if (end > 0)
        f->v.size = (size_t)end;
    void *map = mmap(NULL, f->v.size, PROT_READ, MAP_SHARED, f->v.fd, 0);
    f->v.pixels = map == MAP_FAILED ? NULL : map;
    /* neatvnc's view: the picture's height, the same memory */
    if (ah == h) {
        f->view = f->bo;
    } else {
        struct gbm_import_fd_modifier_data d = {.width = (uint32_t)w,
                                                .height = (uint32_t)h,
                                                .format = DRM_FORMAT_XRGB8888,
                                                .num_fds = 1,
                                                .fds = {f->v.fd},
                                                .strides = {(int)f->v.stride},
                                                .offsets = {0},
                                                .modifier = DRM_FORMAT_MOD_LINEAR};
        f->view = gbm_bo_import(p->gbm, GBM_BO_IMPORT_FD_MODIFIER, &d, GBM_BO_USE_RENDERING);
        if (!f->view) {
            *why = "the buffer object cannot be imported at the picture's height";
            return -1;
        }
    }
    PFNEGLCREATEIMAGEKHRPROC create_image =
        (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!create_image || !image_target) {
        *why = "no EGL images";
        return -1;
    }
    EGLint attrs[] = {EGL_WIDTH,
                      w,
                      EGL_HEIGHT,
                      ah,
                      EGL_LINUX_DRM_FOURCC_EXT,
                      DRM_FORMAT_XRGB8888,
                      EGL_DMA_BUF_PLANE0_FD_EXT,
                      f->v.fd,
                      EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                      0,
                      EGL_DMA_BUF_PLANE0_PITCH_EXT,
                      (EGLint)f->v.stride,
                      EGL_NONE};
    f->image =
        create_image(eglGetCurrentDisplay(), EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
    if (f->image == EGL_NO_IMAGE_KHR) {
        f->image = NULL;
        *why = "EGL cannot import its dma-buf";
        return -1;
    }
    while (glGetError() != GL_NO_ERROR) {
    }
    GLint previous;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGenTextures(1, &f->tex);
    glBindTexture(GL_TEXTURE_2D, f->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    image_target(GL_TEXTURE_2D, f->image);
    glGenFramebuffers(1, &f->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, f->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f->tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
    if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR) {
        *why = "its EGL image is no complete framebuffer";
        return -1;
    }
    return 0;
}

void frames_diff(struct frames *p, struct frame *cur, struct frame *prev, int *tw, int *th)
{
    if (!p->diff)
        p->diff = tilediff_new(p->width, p->height);
    /* the frames' textures are the whole buffer, rows rounded up to 16 */
    tilediff_run(p->diff, cur->tex, prev->tex, (p->height + 15) & ~15, tw, th);
}

void frames_diff_read(struct frames *p, unsigned char *tiles)
{
    tilediff_read(p->diff, tiles);
}

static void pool_close(struct frames *p)
{
    tilediff_free(p->diff);
    p->diff = NULL;
    if (p->frames)
        for (int i = 0; i < p->count; i++)
            frame_fini(&p->frames[i]);
    free(p->frames);
    p->frames = NULL;
    if (p->gbm)
        gbm_device_destroy(p->gbm);
    p->gbm = NULL;
    if (p->drm >= 0)
        close(p->drm);
    p->drm = -1;
}

struct frames *frames_new(int width, int height, int count)
{
    glob_t g;
    if (glob("/dev/dri/renderD*", 0, NULL, &g)) {
        fprintf(stderr, "beanjar-vnc: frames: no render node: read back on the CPU\n");
        return NULL;
    }
    struct frames *p = calloc(1, sizeof *p);
    p->drm = -1;
    p->width = width;
    p->height = height;
    const char *why = "no render node";
    for (size_t n = 0; n < g.gl_pathc; n++) {
        p->drm = open(g.gl_pathv[n], O_RDWR | O_CLOEXEC);
        if (p->drm < 0 || !(p->gbm = gbm_create_device(p->drm))) {
            why = "no GBM device";
            pool_close(p);
            continue;
        }
        p->count = count;
        p->frames = calloc((size_t)count, sizeof *p->frames);
        int i = 0;
        for (; i < count; i++)
            if (frame_init(p, &p->frames[i], width, height, &why))
                break;
        if (i == count) {
            snprintf(p->node, sizeof p->node, "%s", g.gl_pathv[n]);
            globfree(&g);
            return p;
        }
        fprintf(stderr, "beanjar-vnc: frames: %s: %s\n", g.gl_pathv[n], why);
        pool_close(p);
    }
    globfree(&g);
    free(p);
    fprintf(stderr, "beanjar-vnc: frames: no GBM buffers (%s): read back on the CPU\n", why);
    return NULL;
}

struct frame *frames_acquire(struct frames *p)
{
    for (int i = 0; i < p->count; i++) {
        struct frame *f = &p->frames[i];
        if (g_atomic_int_get(&f->v.refs) == 0) {
            g_atomic_int_set(&f->v.refs, 1);
            return f;
        }
    }
    return NULL;
}

const char *frames_node(const struct frames *p)
{
    return p->node;
}

void frames_free(struct frames *p)
{
    if (!p)
        return;
    pool_close(p);
    free(p);
}
