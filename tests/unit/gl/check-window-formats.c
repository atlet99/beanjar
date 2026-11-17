// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The vendor window-config rule (guest/layers/gl/src/egl-window-formats.c) over a fake Mesa
 * whose configs and order are the ones Mesa's Wayland platform offers for a no-alpha request:
 * XRGB8888 before ARGB8888 (smaller buffer first), then RGB565, plus a 10-bit window config. */
#define WINDOW_FORMATS_TEST
#define WINDOW_RGBA {8, 8, 8, 8}, {5, 6, 5, 0}, {5, 5, 5, 1}
#include "../../../guest/layers/gl/src/egl-window-formats.c"
#include <assert.h>
#include <stdio.h>

struct cfg {
    EGLint id, r, g, b, a, type;
};
static const struct cfg table[] = {
    {1, 8, 8, 8, 0, EGL_WINDOW_BIT | EGL_PBUFFER_BIT}, /* XRGB8888 */
    {2, 8, 8, 8, 8, EGL_WINDOW_BIT | EGL_PBUFFER_BIT}, /* ARGB8888 */
    {3, 5, 6, 5, 0, EGL_WINDOW_BIT | EGL_PBUFFER_BIT}, /* RGB565 */
    {4, 10, 10, 10, 0, EGL_WINDOW_BIT},                /* XRGB2101010 */
    {5, 8, 8, 8, 0, EGL_PBUFFER_BIT},                  /* pbuffer only */
};
#define N      (EGLint)(sizeof table / sizeof table[0])
#define CFG(i) ((EGLConfig) & table[i])
static int choose_calls;

static EGLBoolean fake_choose(EGLDisplay d, const EGLint *attr, EGLConfig *cfgs, EGLint size,
                              EGLint *n)
{
    (void)d;
    choose_calls++;
    if (!n)
        return EGL_FALSE;
    EGLint type = EGL_WINDOW_BIT, id = EGL_DONT_CARE;
    for (; attr && attr[0] != EGL_NONE; attr += 2)
        if (attr[0] == EGL_SURFACE_TYPE)
            type = attr[1];
        else if (attr[0] == EGL_CONFIG_ID)
            id = attr[1];
    EGLint k = 0;
    for (EGLint i = 0; i < N; i++) {
        if (id != EGL_DONT_CARE ? table[i].id != id
                                : type != EGL_DONT_CARE && (table[i].type & type) != type)
            continue;
        if (cfgs && k < size)
            cfgs[k] = CFG(i);
        k++;
    }
    *n = cfgs && k > size ? size : k;
    return EGL_TRUE;
}

static EGLBoolean fake_attrib(EGLDisplay d, EGLConfig c, EGLint a, EGLint *v)
{
    (void)d;
    const struct cfg *x = c;
    if (!v)
        return EGL_FALSE;
    switch (a) {
    case EGL_RED_SIZE:
        *v = x->r;
        return EGL_TRUE;
    case EGL_GREEN_SIZE:
        *v = x->g;
        return EGL_TRUE;
    case EGL_BLUE_SIZE:
        *v = x->b;
        return EGL_TRUE;
    case EGL_ALPHA_SIZE:
        *v = x->a;
        return EGL_TRUE;
    case EGL_SURFACE_TYPE:
        *v = x->type;
        return EGL_TRUE;
    case EGL_CONFIG_ID:
        *v = x->id;
        return EGL_TRUE;
    }
    return EGL_FALSE;
}

static void fake_fn(void)
{
}
static __eglMustCastToProperFunctionPointerType fake_proc(const char *name)
{
    (void)name;
    return fake_fn;
}

static int mesa_load(void)
{
    mesa.choose = fake_choose;
    mesa.attrib = fake_attrib;
    mesa.proc = fake_proc;
    return 1;
}

static EGLint id_of(EGLConfig c)
{
    return ((const struct cfg *)c)->id;
}

int main(void)
{
    EGLDisplay d = (EGLDisplay)1;
    EGLConfig out[8];
    EGLint n = -1;

    /* SDL 2.0.4's request, config_size 1: RGBA8888 first, as on the set */
    const EGLint sdl[] = {EGL_RED_SIZE,
                          3,
                          EGL_GREEN_SIZE,
                          3,
                          EGL_BLUE_SIZE,
                          2,
                          EGL_DEPTH_SIZE,
                          16,
                          EGL_SURFACE_TYPE,
                          EGL_WINDOW_BIT,
                          EGL_RENDERABLE_TYPE,
                          EGL_OPENGL_ES2_BIT,
                          EGL_NONE};
    assert(eglChooseConfig(d, sdl, out, 1, &n) && n == 1 && id_of(out[0]) == 2);

    /* NULL configs: the count without the dropped ones (XRGB8888 and 10-bit) */
    n = -1;
    assert(eglChooseConfig(d, sdl, NULL, 0, &n) && n == 2);
    /* room for more: all kept, in Mesa's order */
    assert(eglChooseConfig(d, sdl, out, 8, &n) && n == 2 && id_of(out[0]) == 2 &&
           id_of(out[1]) == 3);
    /* config_size 0 with an array: none */
    assert(eglChooseConfig(d, sdl, out, 0, &n) && n == 0);
    /* no attributes: the window default applies */
    assert(eglChooseConfig(d, NULL, out, 8, &n) && n == 2);

    /* pbuffer requests keep every config Mesa returns */
    const EGLint pb[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    assert(eglChooseConfig(d, pb, out, 8, &n) && n == 4 && id_of(out[0]) == 1);
    const EGLint any[] = {EGL_SURFACE_TYPE, EGL_DONT_CARE, EGL_NONE};
    assert(eglChooseConfig(d, any, out, 8, &n) && n == N);
    /* EGL_CONFIG_ID: everything else ignored, Mesa's answer as it is */
    const EGLint byid[] = {EGL_CONFIG_ID, 1, EGL_NONE};
    assert(eglChooseConfig(d, byid, out, 8, &n) && n == 1 && id_of(out[0]) == 1);

    /* NULL num_config: Mesa's call and its error */
    choose_calls = 0;
    assert(!eglChooseConfig(d, sdl, out, 8, NULL) && choose_calls == 1);

    /* eglGetConfigs callers reading EGL_SURFACE_TYPE see the rule */
    EGLint t;
    assert(eglGetConfigAttrib(d, CFG(0), EGL_SURFACE_TYPE, &t) && t == EGL_PBUFFER_BIT);
    assert(eglGetConfigAttrib(d, CFG(1), EGL_SURFACE_TYPE, &t) &&
           t == (EGL_WINDOW_BIT | EGL_PBUFFER_BIT));
    assert(eglGetConfigAttrib(d, CFG(3), EGL_SURFACE_TYPE, &t) && t == 0);
    assert(eglGetConfigAttrib(d, CFG(4), EGL_SURFACE_TYPE, &t) && t == EGL_PBUFFER_BIT);
    assert(eglGetConfigAttrib(d, CFG(0), EGL_ALPHA_SIZE, &t) && t == 0);
    assert(!eglGetConfigAttrib(d, CFG(0), EGL_SURFACE_TYPE, NULL));

    /* eglGetProcAddress: ours for the two, Mesa's otherwise */
    assert(eglGetProcAddress("eglChooseConfig") ==
           (__eglMustCastToProperFunctionPointerType)eglChooseConfig);
    assert(eglGetProcAddress("eglGetConfigAttrib") ==
           (__eglMustCastToProperFunctionPointerType)eglGetConfigAttrib);
    assert(eglGetProcAddress("eglSwapBuffers") == fake_fn);
    puts("ok");
    return 0;
}
