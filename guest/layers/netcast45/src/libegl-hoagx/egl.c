// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libEGL.so for NetCast 4.5 (docs/dev/architecture/netcast45.md, phase 2): the window system of
 * LG's Mali "hoagx" EGL, over Mesa. The stock libEGL (Mali r3p2-01rel3) takes EGL_DEFAULT_DISPLAY
 * and an HOA_GX window id as the native window, wraps the window's two GX surfaces
 * (HOA_GX_GetWindowSurfaceInfo) as its render targets and calls HOA_GX_UpdateWindow(window, NULL)
 * in eglSwapBuffers; procmgr flips and shows the window. Here Mesa renders (its surfaceless
 * platform: virgl on a render node, else softpipe) into a pbuffer of the window's size, and
 * eglSwapBuffers copies the frame into the window's GX surfaces through the pool mapping
 * libafopenapi made (record +4), then HOA_GX_UpdateWindow. Mesa's own libEGL is dlopened from
 * MESA_EGL; every function this file does not define is Mesa's, through eglGetProcAddress. A config
 * is offered for windows only when the window's surface format can take it (8-bit RGB with or
 * without alpha into ARGB8888); anything else fails with the EGL error, never pretends.
 * LIBEGL_TRACE=1 logs every call this file handles. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))
#ifndef MESA_EGL
#define MESA_EGL "libEGLmesa.so.1"
#endif

/* ---- LG's window system, from the process (libafopenapi or liblgopenapi) ---- */

/* the window layer's surface record (libafopenapi HOA_GX_GetWindowSurfaceInfo, 1064 bytes):
 * +0 pool offset, +4 its address in this process, +8 the GAL handle, then the GAL surface's
 * pitch, bpp, width, height (u16) and format (u32) */
typedef struct {
    uint32_t offset;
    uint32_t address;
    uint32_t handle;
    uint16_t pitch, bpp, width, height;
    uint32_t format;
    uint8_t rest[1064 - 24];
} gx_surface_t;
_Static_assert(sizeof(gx_surface_t) == 1064, "HOA_GX window surface record");
#define GX_FORMAT_ARGB8888 0

typedef int (*gx_surface_info_fn)(uint32_t window, gx_surface_t *a, gx_surface_t *b);
typedef int (*gx_resolution_fn)(uint32_t window, uint16_t *w, uint16_t *h);
typedef int (*gx_update_fn)(uint32_t window, void *rect);

static void *gx(const char *name)
{
    return dlsym(RTLD_DEFAULT, name);
}

/* ---- Mesa ---- */

static void *mesa;
static pthread_once_t mesa_once = PTHREAD_ONCE_INIT;
static __eglMustCastToProperFunctionPointerType (*real_proc)(const char *);
static int trace;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[libEGL] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
#define TRACE(...)                                                                                 \
    do {                                                                                           \
        if (trace)                                                                                 \
            say(__VA_ARGS__);                                                                      \
    } while (0)

static void mesa_load(void)
{
    trace = getenv("LIBEGL_TRACE") && getenv("LIBEGL_TRACE")[0] == '1';
    mesa = dlopen(MESA_EGL, RTLD_NOW | RTLD_LOCAL);
    if (!mesa) {
        say("%s: %s", MESA_EGL, dlerror());
        return;
    }
    real_proc =
        (__eglMustCastToProperFunctionPointerType(*)(const char *))dlsym(mesa, "eglGetProcAddress");
}

static void *real(const char *name)
{
    void *f;
    pthread_once(&mesa_once, mesa_load);
    if (!mesa)
        return NULL;
    f = dlsym(mesa, name);
    if (!f && real_proc)
        f = (void *)real_proc(name);
    return f;
}
#define REAL(type, name) ((type)real(#name))

/* the error of a call this file refused, or EGL_SUCCESS: eglGetError reports it once */
static __thread EGLint own_error = EGL_SUCCESS;
static EGLBoolean fail(EGLint error)
{
    own_error = error;
    return EGL_FALSE;
}
static void ok(void)
{
    own_error = EGL_SUCCESS;
}

/* ---- the display ---- */

EXPORT EGLDisplay eglGetDisplay(EGLNativeDisplayType native)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get =
        REAL(PFNEGLGETPLATFORMDISPLAYEXTPROC, eglGetPlatformDisplayEXT);
    if (native != EGL_DEFAULT_DISPLAY) {
        say("eglGetDisplay(%p): only EGL_DEFAULT_DISPLAY is a display here", (void *)native);
        return EGL_NO_DISPLAY;
    }
    if (!get)
        return EGL_NO_DISPLAY;
    EGLDisplay d = get(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    TRACE("eglGetDisplay -> %p", d);
    return d;
}

EXPORT EGLint eglGetError(void)
{
    EGLint e = own_error;
    EGLint r = REAL(PFNEGLGETERRORPROC, eglGetError) ? REAL(PFNEGLGETERRORPROC, eglGetError)()
                                                     : EGL_NOT_INITIALIZED;
    own_error = EGL_SUCCESS;
    return e != EGL_SUCCESS ? e : r;
}

/* ---- configs: a window is a pbuffer underneath ---- */

static int presentable(EGLDisplay d, EGLConfig c)
{
    PFNEGLGETCONFIGATTRIBPROC attr = REAL(PFNEGLGETCONFIGATTRIBPROC, eglGetConfigAttrib);
    EGLint r = 0, g = 0, b = 0, a = 0, type = 0;
    attr(d, c, EGL_RED_SIZE, &r);
    attr(d, c, EGL_GREEN_SIZE, &g);
    attr(d, c, EGL_BLUE_SIZE, &b);
    attr(d, c, EGL_ALPHA_SIZE, &a);
    attr(d, c, EGL_SURFACE_TYPE, &type);
    return r == 8 && g == 8 && b == 8 && (a == 8 || a == 0) && (type & EGL_PBUFFER_BIT);
}

EXPORT EGLBoolean eglGetConfigAttrib(EGLDisplay d, EGLConfig c, EGLint attribute, EGLint *value)
{
    EGLBoolean r = REAL(PFNEGLGETCONFIGATTRIBPROC, eglGetConfigAttrib)(d, c, attribute, value);
    if (r && attribute == EGL_SURFACE_TYPE && presentable(d, c))
        *value |= EGL_WINDOW_BIT;
    return r;
}

EXPORT EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint *attribs, EGLConfig *configs,
                                  EGLint size, EGLint *num)
{
    PFNEGLCHOOSECONFIGPROC choose = REAL(PFNEGLCHOOSECONFIGPROC, eglChooseConfig);
    EGLint list[128], n = 0, want_window = 1; /* EGL_SURFACE_TYPE's default is EGL_WINDOW_BIT */
    int has_type = 0;
    if (!num)
        return fail(EGL_BAD_PARAMETER);
    for (const EGLint *p = attribs; p && *p != EGL_NONE; p += 2) {
        if (n + 4 >= (EGLint)(sizeof(list) / sizeof(list[0])))
            return fail(EGL_BAD_ATTRIBUTE);
        list[n] = p[0];
        list[n + 1] = p[1];
        if (p[0] == EGL_SURFACE_TYPE) {
            has_type = 1;
            want_window = p[1] != (EGLint)EGL_DONT_CARE && (p[1] & EGL_WINDOW_BIT);
            if (want_window)
                list[n + 1] = (p[1] & ~EGL_WINDOW_BIT) | EGL_PBUFFER_BIT;
        }
        n += 2;
    }
    if (!has_type) {
        list[n++] = EGL_SURFACE_TYPE;
        list[n++] = EGL_PBUFFER_BIT;
    }
    list[n] = EGL_NONE;
    if (!want_window) {
        EGLBoolean r = choose(d, list, configs, size, num);
        TRACE("eglChooseConfig (no window) -> %d configs", r ? *num : -1);
        return r;
    }
    /* the configs a window can show, in Mesa's order */
    EGLint all = 0;
    if (!choose(d, list, NULL, 0, &all))
        return EGL_FALSE;
    EGLConfig *tmp = calloc(all ? all : 1, sizeof(*tmp));
    if (!tmp)
        return fail(EGL_BAD_ALLOC);
    if (!choose(d, list, tmp, all, &all)) {
        free(tmp);
        return EGL_FALSE;
    }
    EGLint out = 0;
    for (EGLint i = 0; i < all; i++) {
        if (!presentable(d, tmp[i]))
            continue;
        if (configs && out < size)
            configs[out] = tmp[i];
        if (configs ? out < size : 1)
            out++;
    }
    free(tmp);
    *num = out;
    TRACE("eglChooseConfig (window) -> %d configs", out);
    ok();
    return EGL_TRUE;
}

/* ---- window surfaces ---- */

struct window {
    struct window *next;
    EGLDisplay dpy;
    EGLSurface surf;
    uint32_t id;
    EGLint width, height;
    uint32_t *pixels; /* a frame read back, RGBA */
    unsigned frames, refused, size_logged;
    int check_size;
};
static struct window *windows;
static pthread_mutex_t windows_lock = PTHREAD_MUTEX_INITIALIZER;

static struct window *window_of(EGLSurface s)
{
    struct window *w;
    pthread_mutex_lock(&windows_lock);
    for (w = windows; w && w->surf != s; w = w->next)
        ;
    pthread_mutex_unlock(&windows_lock);
    return w;
}

EXPORT EGLSurface eglCreateWindowSurface(EGLDisplay d, EGLConfig c, EGLNativeWindowType native,
                                         const EGLint *attribs)
{
    gx_resolution_fn res = (gx_resolution_fn)gx("HOA_GX_GetWindowResolution");
    uint32_t id = (uint32_t)(uintptr_t)native;
    uint16_t w = 0, h = 0;
    EGLint pb[16], n = 0;
    (void)attribs; /* EGL_RENDER_BUFFER and the colourspace: a pbuffer's defaults */
    if (!res || res(id, &w, &h) != 0 || !w || !h) {
        say("eglCreateWindowSurface: window %u: no such HOA_GX window", id);
        fail(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }
    if (!presentable(d, c)) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_SURFACE;
    }
    pb[n++] = EGL_WIDTH;
    pb[n++] = w;
    pb[n++] = EGL_HEIGHT;
    pb[n++] = h;
    pb[n] = EGL_NONE;
    EGLSurface s = REAL(PFNEGLCREATEPBUFFERSURFACEPROC, eglCreatePbufferSurface)(d, c, pb);
    if (s == EGL_NO_SURFACE)
        return s;
    struct window *win = calloc(1, sizeof(*win));
    if (!win || !(win->pixels = malloc((size_t)w * h * 4))) {
        free(win);
        REAL(PFNEGLDESTROYSURFACEPROC, eglDestroySurface)(d, s);
        fail(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }
    win->dpy = d;
    win->surf = s;
    win->id = id;
    win->width = w;
    win->height = h;
    pthread_mutex_lock(&windows_lock);
    win->next = windows;
    windows = win;
    pthread_mutex_unlock(&windows_lock);
    say("window %u: %ux%u surface (pid %d)", id, w, h, getpid());
    ok();
    return s;
}

EXPORT EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface s)
{
    pthread_mutex_lock(&windows_lock);
    for (struct window **p = &windows; *p; p = &(*p)->next) {
        if ((*p)->surf == s) {
            struct window *w = *p;
            *p = w->next;
            free(w->pixels);
            free(w);
            break;
        }
    }
    pthread_mutex_unlock(&windows_lock);
    return REAL(PFNEGLDESTROYSURFACEPROC, eglDestroySurface)(d, s);
}

EXPORT EGLSurface eglCreatePixmapSurface(EGLDisplay d, EGLConfig c, EGLNativePixmapType p,
                                         const EGLint *a)
{
    (void)d;
    (void)c;
    (void)p;
    (void)a;
    fail(EGL_BAD_NATIVE_PIXMAP); /* the hoagx window system has no pixmaps */
    return EGL_NO_SURFACE;
}

/* the pbuffer's pixels (GL's bottom-up RGBA) into one ARGB8888 GX surface, top-down */
static void put(const struct window *w, const gx_surface_t *s)
{
    unsigned width = s->width < (unsigned)w->width ? s->width : (unsigned)w->width;
    unsigned height = s->height < (unsigned)w->height ? s->height : (unsigned)w->height;
    uint8_t *base = (uint8_t *)(uintptr_t)s->address;
    for (unsigned y = 0; y < height; y++) {
        const uint8_t *src = (const uint8_t *)(w->pixels + (size_t)(w->height - 1 - y) * w->width);
        uint32_t *dst = (uint32_t *)(base + (size_t)y * s->pitch);
        for (unsigned x = 0; x < width; x++, src += 4)
            dst[x] =
                (uint32_t)src[3] << 24 | (uint32_t)src[0] << 16 | (uint32_t)src[1] << 8 | src[2];
    }
}

EXPORT EGLBoolean eglSwapBuffers(EGLDisplay d, EGLSurface s)
{
    struct window *w = window_of(s);
    if (!w)
        return REAL(PFNEGLSWAPBUFFERSPROC, eglSwapBuffers)(d, s);
    if (REAL(PFNEGLGETCURRENTSURFACEPROC, eglGetCurrentSurface)(EGL_DRAW) != s)
        return fail(EGL_BAD_SURFACE);
    gx_surface_info_fn info = (gx_surface_info_fn)gx("HOA_GX_GetWindowSurfaceInfo");
    gx_update_fn update = (gx_update_fn)gx("HOA_GX_UpdateWindow");
    void (*get_iv)(GLenum, GLint *) = REAL(void (*)(GLenum, GLint *), glGetIntegerv);
    void (*bind_fb)(GLenum, GLuint) = REAL(void (*)(GLenum, GLuint), glBindFramebuffer);
    void (*store)(GLenum, GLint) = REAL(void (*)(GLenum, GLint), glPixelStorei);
    void (*read)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) =
        REAL(void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *), glReadPixels);
    gx_surface_t a, b;
    GLint fb = 0, align = 4;
    if (!info || !update || !get_iv || !bind_fb || !store || !read)
        return fail(EGL_BAD_NATIVE_WINDOW);
    if (w->check_size) {
        gx_resolution_fn res = (gx_resolution_fn)gx("HOA_GX_GetWindowResolution");
        uint16_t rw = 0, rh = 0;
        w->check_size = 0;
        if (res && res(w->id, &rw, &rh) == 0 && (rw != w->width || rh != w->height) &&
            !w->size_logged++)
            say("window %u: now %ux%u, the surface stays %dx%d (resizing is not implemented)",
                w->id, rw, rh, w->width, w->height);
    }
    struct timespec t0, t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    get_iv(GL_FRAMEBUFFER_BINDING, &fb);
    get_iv(GL_PACK_ALIGNMENT, &align);
    if (fb)
        bind_fb(GL_FRAMEBUFFER, 0);
    store(GL_PACK_ALIGNMENT, 4);
    read(0, 0, w->width, w->height, GL_RGBA, GL_UNSIGNED_BYTE, w->pixels);
    store(GL_PACK_ALIGNMENT, align);
    if (fb)
        bind_fb(GL_FRAMEBUFFER, (GLuint)fb);
    if (info(w->id, &a, &b) != 0) {
        say("window %u: HOA_GX_GetWindowSurfaceInfo failed", w->id);
        return fail(EGL_BAD_NATIVE_WINDOW);
    }
    if (a.format != GX_FORMAT_ARGB8888 || a.bpp != 32 || !a.address ||
        b.format != GX_FORMAT_ARGB8888 || b.bpp != 32 || !b.address) {
        say("window %u: surfaces of format %u/%u bpp %u/%u at %#x/%#x: not ARGB8888 in this "
            "process",
            w->id, a.format, b.format, a.bpp, b.bpp, a.address, b.address);
        return fail(EGL_BAD_NATIVE_WINDOW);
    }
    /* Both of the window's surfaces get the frame: which one procmgr shows after its flip
     * (PM_GX_UpdateWindowByFlip copies one to the other, or flips and sets it as a frame
     * buffer) is its business. */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    put(w, &a);
    put(w, &b);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    /* procmgr refuses to update a window that is not visible ("visibility is FALSE. So this window
     * can't be updated"); the frame is in its surfaces all the same. As the stock libEGL, that is
     * not a failed swap (its UpdateWindow callback only marks the surface to check the window's
     * size at the next frame: __egl_mali_post_to_window_surface). */
    int r = update(w->id, NULL);
    if (r != 0 && w->refused++ < 2)
        say("window %u: HOA_GX_UpdateWindow: %d (not shown; procmgr refuses an invisible window)",
            w->id, r);
    if (r != 0)
        w->check_size = 1;
    else
        w->refused = 0;
    if (w->frames++ < 2 || w->frames % 500 == 0 || trace)
        say("window %u: frame %u, %dx%d into %ux%u surfaces %#x and %#x (read %ld us, copy %ld us)",
            w->id, w->frames, w->width, w->height, a.width, a.height, a.offset, b.offset,
            (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000,
            (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_nsec - t1.tv_nsec) / 1000);
    ok();
    return EGL_TRUE;
}

EXPORT EGLBoolean eglCopyBuffers(EGLDisplay d, EGLSurface s, EGLNativePixmapType p)
{
    (void)d;
    (void)s;
    (void)p;
    return fail(EGL_BAD_NATIVE_PIXMAP);
}

/* ---- the rest of EGL 1.4: Mesa's ---- */

#define PASS(ret, name, params, args)                                                              \
    EXPORT ret name params                                                                         \
    {                                                                                              \
        ok();                                                                                      \
        return REAL(ret(*) params, name) args;                                                     \
    }

PASS(EGLBoolean, eglInitialize, (EGLDisplay d, EGLint *major, EGLint *minor), (d, major, minor))
PASS(EGLBoolean, eglTerminate, (EGLDisplay d), (d))
PASS(const char *, eglQueryString, (EGLDisplay d, EGLint name), (d, name))
PASS(EGLBoolean, eglGetConfigs, (EGLDisplay d, EGLConfig *c, EGLint size, EGLint *num),
     (d, c, size, num))
PASS(EGLSurface, eglCreatePbufferSurface, (EGLDisplay d, EGLConfig c, const EGLint *a), (d, c, a))
PASS(EGLBoolean, eglQuerySurface, (EGLDisplay d, EGLSurface s, EGLint a, EGLint *v), (d, s, a, v))
PASS(EGLBoolean, eglBindAPI, (EGLenum api), (api))
PASS(EGLenum, eglQueryAPI, (void), ())
PASS(EGLBoolean, eglWaitClient, (void), ())
PASS(EGLBoolean, eglReleaseThread, (void), ())
PASS(EGLSurface, eglCreatePbufferFromClientBuffer,
     (EGLDisplay d, EGLenum t, EGLClientBuffer b, EGLConfig c, const EGLint *a), (d, t, b, c, a))
PASS(EGLBoolean, eglSurfaceAttrib, (EGLDisplay d, EGLSurface s, EGLint a, EGLint v), (d, s, a, v))
PASS(EGLBoolean, eglBindTexImage, (EGLDisplay d, EGLSurface s, EGLint b), (d, s, b))
PASS(EGLBoolean, eglReleaseTexImage, (EGLDisplay d, EGLSurface s, EGLint b), (d, s, b))
PASS(EGLBoolean, eglSwapInterval, (EGLDisplay d, EGLint i), (d, i))
PASS(EGLContext, eglCreateContext, (EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *a),
     (d, c, share, a))
PASS(EGLBoolean, eglDestroyContext, (EGLDisplay d, EGLContext c), (d, c))
PASS(EGLBoolean, eglMakeCurrent, (EGLDisplay d, EGLSurface dr, EGLSurface rd, EGLContext c),
     (d, dr, rd, c))
PASS(EGLContext, eglGetCurrentContext, (void), ())
PASS(EGLSurface, eglGetCurrentSurface, (EGLint which), (which))
PASS(EGLDisplay, eglGetCurrentDisplay, (void), ())
PASS(EGLBoolean, eglQueryContext, (EGLDisplay d, EGLContext c, EGLint a, EGLint *v), (d, c, a, v))
PASS(EGLBoolean, eglWaitGL, (void), ())
PASS(EGLBoolean, eglWaitNative, (EGLint engine), (engine))

EXPORT __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    /* this file's EGL functions, so that a caller through eglGetProcAddress gets the window system
     */
    static const struct {
        const char *name;
        void *f;
    } own[] = {
        {"eglGetDisplay", (void *)eglGetDisplay},
        {"eglGetError", (void *)eglGetError},
        {"eglChooseConfig", (void *)eglChooseConfig},
        {"eglGetConfigAttrib", (void *)eglGetConfigAttrib},
        {"eglCreateWindowSurface", (void *)eglCreateWindowSurface},
        {"eglCreatePixmapSurface", (void *)eglCreatePixmapSurface},
        {"eglDestroySurface", (void *)eglDestroySurface},
        {"eglSwapBuffers", (void *)eglSwapBuffers},
        {"eglCopyBuffers", (void *)eglCopyBuffers},
    };
    for (unsigned i = 0; name && i < sizeof(own) / sizeof(own[0]); i++)
        if (!strcmp(name, own[i].name))
            return (__eglMustCastToProperFunctionPointerType)own[i].f;
    pthread_once(&mesa_once, mesa_load);
    return real_proc ? real_proc(name) : NULL;
}
