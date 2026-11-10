// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* TEST-ONLY LD_PRELOAD shim: lets a firmware EGL client that expects a window surface
 * (e.g. Qt's minimalegl plugin, which passes native window 0) run on Mesa's surfaceless
 * platform. Window surfaces become pbuffers of SHIM_SIZE (default 160x120); every
 * eglSwapBuffers reads the frame back, prints a summary, and writes /tmp/shim-frameN.ppm.
 * After SHIM_FRAMES frames (default 3) the process exits(0).
 * This is not the production native-window path; see guest/layers/gl/README notes in the report. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int W = 160, H = 120, frames;
static EGLConfig win_cfg; /* config of the window-turned-pbuffer surface */

#define REAL(name)                                                                                 \
    static __typeof__(name) *real_##name;                                                          \
    if (!real_##name)                                                                              \
    real_##name = (__typeof__(name) *)dlsym(RTLD_NEXT, #name)

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attr, EGLConfig *cfgs, EGLint size,
                           EGLint *n)
{
    REAL(eglChooseConfig);
    EGLint a[128];
    int i = 0;
    for (; attr && attr[i] != EGL_NONE && i < 126; i += 2) {
        a[i] = attr[i];
        a[i + 1] = attr[i] == EGL_SURFACE_TYPE ? EGL_PBUFFER_BIT : attr[i + 1];
    }
    a[i] = EGL_NONE;
    return real_eglChooseConfig(dpy, a, cfgs, size, n);
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg, EGLNativeWindowType win,
                                  const EGLint *attr)
{
    const char *s = getenv("SHIM_SIZE");
    if (s)
        sscanf(s, "%dx%d", &W, &H);
    const EGLint pattr[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
    fprintf(stderr, "[shim] eglCreateWindowSurface(win=%p) -> pbuffer %dx%d\n", (void *)win, W, H);
    win_cfg = cfg;
    return eglCreatePbufferSurface(dpy, cfg, pattr);
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surf)
{
    static unsigned char px[4096 * 4096 * 4 / 16];
    char name[64];
    if ((size_t)W * H * 4 > sizeof px)
        return EGL_TRUE;
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    unsigned long sum = 0;
    for (int i = 0; i < W * H * 4; i++)
        sum = sum * 31 + px[i];
    unsigned char *c = px + 4 * ((H / 2) * W + W / 2), *o = px + 4 * (2 * W + W - 3);
    fprintf(stderr,
            "[shim] frame %d: centre=%u,%u,%u,%u corner=%u,%u,%u,%u hash=%08lx renderer=%s\n",
            frames, c[0], c[1], c[2], c[3], o[0], o[1], o[2], o[3], sum & 0xffffffff,
            (const char *)glGetString(GL_RENDERER));
    snprintf(name, sizeof name, "/tmp/shim-frame%d.ppm", frames);
    FILE *f = fopen(name, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int y = H - 1; y >= 0; y--)
            for (int x = 0; x < W; x++)
                fwrite(px + 4 * (y * W + x), 1, 3, f);
        fclose(f);
    }
    const char *m = getenv("SHIM_FRAMES");
    if (++frames >= (m ? atoi(m) : 3)) {
        fflush(stderr);
        _exit(0);
    }
    return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface d, EGLSurface r, EGLContext c)
{
    REAL(eglMakeCurrent);
    EGLBoolean ok = real_eglMakeCurrent(dpy, d, r, c);
    if (!ok || getenv("SHIM_DEBUG"))
        fprintf(stderr, "[shim] eglMakeCurrent(%p,%p,%p) = %d err=0x%x\n", d, r, c, ok,
                eglGetError());
    return ok;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share, const EGLint *attr)
{
    REAL(eglCreateContext);
    /* Qt picks the context config separately; Mesa then rejects it with the pbuffer
     * (EGL_BAD_MATCH). Use the surface's config instead. */
    EGLContext c = real_eglCreateContext(dpy, win_cfg ? win_cfg : cfg, share, attr);
    fprintf(stderr, "[shim] eglCreateContext(cfg=%p -> %p) = %p\n", cfg, win_cfg ? win_cfg : cfg,
            c);
    return c;
}
