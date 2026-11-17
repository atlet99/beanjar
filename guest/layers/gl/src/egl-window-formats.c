// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The vendor's one GL library (gl_vendor_gl) with the vendor driver's window-config rule: a
 * config is window-capable only if its RGBA sizes are one of the formats the vendor's own
 * Wayland server side offers (WINDOW_RGBA, the soc table's gl_vendor_window_rgba). Other configs
 * stay, for pbuffers and pixmaps. Mesa offers window configs for every format the compositor
 * takes, XRGB8888 among them; an application that asks for no alpha (SDL's default) then gets
 * an opaque window where the set gives it an ARGB8888 one, and video under it does not show.
 *
 * Like the empty stand-in (pvr-stub.c) it NEEDs Mesa's libEGL, libGLESv2 and libGLESv1_CM, so
 * everything it does not define comes from Mesa, by global lookup or by dlsym on its handle;
 * Mesa's libEGL by another file name, libEGL-mesa.so.1 (MESA_EGL).
 * It defines:
 *   eglChooseConfig     a request that includes EGL_WINDOW_BIT (the default) drops the configs
 *                       the rule takes the window bit from; Mesa's order otherwise
 *   eglGetConfigAttrib  EGL_SURFACE_TYPE without EGL_WINDOW_BIT for them
 *   eglGetProcAddress   these two for their names
 * The gl layer also points libEGL.so, libEGL.so.1 and libEGL.so.1.4 at this library, as the
 * vendor's libEGL.so.1.4 is a wrapper NEEDing it: SDL dlopens libEGL.so.1 or libEGL.so and looks
 * up eglChooseConfig on that handle. Its SONAME is libEGL.so.1, Mesa's too: loaded before Mesa
 * (only it NEEDs MESA_EGL), it is the object a later libEGL.so.1 by name resolves to.
 *
 * The rule and its formats come from the vendor driver of the target (soc table). */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))

#ifndef WINDOW_RGBA
#error WINDOW_RGBA: the window formats as {r, g, b, a}, ... (gl_vendor_window_rgba)
#endif
static const EGLint window_rgba[][4] = {WINDOW_RGBA};
#define MESA_EGL "libEGL-mesa.so.1"

static struct {
    __typeof__(eglChooseConfig) *choose;
    __typeof__(eglGetConfigAttrib) *attrib;
    __typeof__(eglGetProcAddress) *proc;
} mesa;

static int mesa_load(void); /* the unit test's fake Mesa has its own */
#ifndef WINDOW_FORMATS_TEST
/* Mesa's own definitions: dlsym on its handle searches Mesa first (a global lookup or
 * RTLD_NEXT could find another interposer, such as the compositor's libegl-kms). By MESA_EGL,
 * as libEGL.so.1 is this library. */
static int mesa_load(void)
{
    if (mesa.choose && mesa.attrib && mesa.proc)
        return 1;
    void *h = dlopen(MESA_EGL, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    if (!h)
        return 0;
    mesa.attrib = (__typeof__(mesa.attrib))dlsym(h, "eglGetConfigAttrib");
    mesa.proc = (__typeof__(mesa.proc))dlsym(h, "eglGetProcAddress");
    mesa.choose = (__typeof__(mesa.choose))dlsym(h, "eglChooseConfig");
    return mesa.choose && mesa.attrib && mesa.proc;
}
#endif

static int window_allowed(EGLDisplay dpy, EGLConfig cfg)
{
    static const EGLint names[4] = {EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE};
    EGLint v[4] = {0};
    for (int i = 0; i < 4; i++)
        if (!mesa.attrib(dpy, cfg, names[i], &v[i]))
            return 1; /* not ours to judge */
    for (size_t f = 0; f < sizeof window_rgba / sizeof window_rgba[0]; f++)
        if (!memcmp(v, window_rgba[f], sizeof v))
            return 1;
    return 0;
}

/* Whether the request needs a window: EGL_SURFACE_TYPE defaults to EGL_WINDOW_BIT, the last
 * one given counts, and with an EGL_CONFIG_ID every other attribute is ignored. */
static int window_requested(const EGLint *attr)
{
    EGLint type = EGL_WINDOW_BIT, id = EGL_DONT_CARE;
    for (; attr && attr[0] != EGL_NONE; attr += 2) {
        if (attr[0] == EGL_SURFACE_TYPE)
            type = attr[1];
        else if (attr[0] == EGL_CONFIG_ID)
            id = attr[1];
    }
    return id == EGL_DONT_CARE && type != EGL_DONT_CARE && (type & EGL_WINDOW_BIT);
}

EXPORT EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attr, EGLConfig *cfgs, EGLint size,
                                  EGLint *n)
{
    if (!mesa_load())
        return EGL_FALSE;
    if (!n || !window_requested(attr))
        return mesa.choose(dpy, attr, cfgs, size, n); /* Mesa's errors as they are */
    EGLint total = 0;
    if (!mesa.choose(dpy, attr, NULL, 0, &total))
        return EGL_FALSE;
    EGLConfig *all = calloc(total > 0 ? total : 1, sizeof *all);
    if (!all)
        return EGL_FALSE;
    if (!mesa.choose(dpy, attr, all, total, &total)) {
        free(all);
        return EGL_FALSE;
    }
    EGLint k = 0;
    for (EGLint j = 0; j < total; j++) {
        if (!window_allowed(dpy, all[j]))
            continue;
        if (cfgs && k >= size)
            break;
        if (cfgs)
            cfgs[k] = all[j];
        k++;
    }
    free(all);
    *n = k;
    return EGL_TRUE;
}

EXPORT EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig cfg, EGLint a, EGLint *v)
{
    if (!mesa_load() || !mesa.attrib(dpy, cfg, a, v))
        return EGL_FALSE;
    if (a == EGL_SURFACE_TYPE && (*v & EGL_WINDOW_BIT) && !window_allowed(dpy, cfg))
        *v &= ~EGL_WINDOW_BIT;
    return EGL_TRUE;
}

EXPORT __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    if (name && !strcmp(name, "eglChooseConfig"))
        return (__eglMustCastToProperFunctionPointerType)eglChooseConfig;
    if (name && !strcmp(name, "eglGetConfigAttrib"))
        return (__eglMustCastToProperFunctionPointerType)eglGetConfigAttrib;
    return mesa_load() ? mesa.proc(name) : NULL;
}
