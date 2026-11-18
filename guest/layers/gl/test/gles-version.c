// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The OpenGL ES version the guest gives an app: for each client version 2.0, 3.0, 3.1 and 3.2,
 * whether eglCreateContext gives a context (else its EGL error), and in the ones it gives,
 * GL_VERSION, GL_SHADING_LANGUAGE_VERSION and whether a "#version 300 es" shader compiles.
 * With the gl layer's /usr/share/beanjar/gles-version the highest is at most that version, and
 * never more than the driver gives without it (patches/mesa/0005-*).
 *
 *   gles-version        (surfaceless EGL on the render node; one line per client version)
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>

static const char *egl_error(EGLint e)
{
    switch (e) {
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_CONFIG:
        return "EGL_BAD_CONFIG";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_ALLOC:
        return "EGL_BAD_ALLOC";
    default:
        return "another EGL error";
    }
}

static int compiles(const char *src)
{
    GLuint s = glCreateShader(GL_FRAGMENT_SHADER);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    glDeleteShader(s);
    return ok;
}

int main(void)
{
    static const EGLint versions[][2] = {{2, 0}, {3, 0}, {3, 1}, {3, 2}};
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy =
        gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
    EGLint maj, min, n;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min)) {
        fprintf(stderr, "FAIL: no surfaceless EGL display (0x%x)\n", eglGetError());
        return 1;
    }
    const EGLint cattr[] = {EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
    EGLConfig cfg;
    if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1) {
        fprintf(stderr, "FAIL: no ES2 config (0x%x)\n", eglGetError());
        return 1;
    }
    EGLint apis = 0;
    eglGetConfigAttrib(dpy, cfg, EGL_RENDERABLE_TYPE, &apis);
    printf("config: EGL_OPENGL_ES3_BIT %s\n", apis & EGL_OPENGL_ES3_BIT_KHR ? "yes" : "no");
    eglBindAPI(EGL_OPENGL_ES_API);
    for (unsigned i = 0; i < sizeof versions / sizeof versions[0]; i++) {
        const EGLint attr[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, versions[i][0],
                               EGL_CONTEXT_MINOR_VERSION_KHR, versions[i][1], EGL_NONE};
        EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attr);
        printf("request %d.%d: ", versions[i][0], versions[i][1]);
        if (ctx == EGL_NO_CONTEXT) {
            printf("refused, %s\n", egl_error(eglGetError()));
            continue;
        }
        if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
            printf("made, but not current (0x%x)\n", eglGetError());
            eglDestroyContext(dpy, ctx);
            continue;
        }
        printf("GL_VERSION \"%s\", GLSL \"%s\", #version 300 es %s\n",
               (const char *)glGetString(GL_VERSION),
               (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION),
               compiles("#version 300 es\nprecision mediump float;\nout vec4 c;\n"
                        "void main() { c = vec4(1.0); }\n")
                   ? "compiles"
                   : "refused");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);
    }
    eglTerminate(dpy);
    return 0;
}
