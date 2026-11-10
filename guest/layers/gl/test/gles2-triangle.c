// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* GLES2 smoke test: EGL surfaceless (or pbuffer), clear to blue, draw a red triangle,
 * read back pixels. Built by the guest build (guest/layers/gl/test/meson.build). */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 64

static void die(const char *m)
{
    fprintf(stderr, "FAIL: %s (egl 0x%x gl 0x%x)\n", m, eglGetError(), glGetError());
    exit(1);
}

static GLuint shader(GLenum t, const char *src)
{
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "%s\n", log);
        die("compile");
    }
    return s;
}

int main(int argc, char **argv)
{
    int use_pbuffer = argc > 1 && !strcmp(argv[1], "pbuffer");
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    printf("client extensions: %s\n", eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
    EGLDisplay dpy = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL)
                         : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min))
        die("eglInitialize");
    printf("EGL %d.%d vendor=%s version=%s\n", maj, min, eglQueryString(dpy, EGL_VENDOR),
           eglQueryString(dpy, EGL_VERSION));

    const EGLint cattr[] = {EGL_SURFACE_TYPE,
                            use_pbuffer ? EGL_PBUFFER_BIT : 0,
                            EGL_RENDERABLE_TYPE,
                            EGL_OPENGL_ES2_BIT,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_NONE};
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1)
        die("eglChooseConfig");
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ctxattr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT)
        die("eglCreateContext");

    EGLSurface surf = EGL_NO_SURFACE;
    GLuint fbo = 0, rb = 0;
    if (use_pbuffer) {
        const EGLint pattr[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
        surf = eglCreatePbufferSurface(dpy, cfg, pattr);
        if (surf == EGL_NO_SURFACE)
            die("eglCreatePbufferSurface");
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx))
        die("eglMakeCurrent");
    if (!use_pbuffer) { /* surfaceless: render into an FBO */
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenRenderbuffers(1, &rb);
        glBindRenderbuffer(GL_RENDERBUFFER, rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB565, W, H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            die("fbo");
    }
    printf("mode=%s GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\nGLSL=%s\n",
           use_pbuffer ? "pbuffer" : "surfaceless+fbo", glGetString(GL_VENDOR),
           glGetString(GL_RENDERER), glGetString(GL_VERSION),
           glGetString(GL_SHADING_LANGUAGE_VERSION));

    GLuint p = glCreateProgram();
    glAttachShader(
        p, shader(GL_VERTEX_SHADER,
                  "attribute vec2 pos; void main() { gl_Position = vec4(pos, 0.0, 1.0); }"));
    glAttachShader(
        p,
        shader(
            GL_FRAGMENT_SHADER,
            "precision mediump float; void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }"));
    glBindAttribLocation(p, 0, "pos");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
        die("link");

    static const GLfloat tri[] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f};
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(p);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();

    unsigned char px[W * H * 4];
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    if (glGetError())
        die("glReadPixels");
    static const int pts[][2] = {{32, 24}, {2, 2}, {61, 61}, {16, 40}};
    int pass = 1;
    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        unsigned char *c = px + 4 * (pts[i][1] * W + pts[i][0]);
        printf("pixel(%2d,%2d) = %3u %3u %3u %3u\n", pts[i][0], pts[i][1], c[0], c[1], c[2], c[3]);
    }
    unsigned char *in = px + 4 * (24 * W + 32), *out = px + 4 * (2 * W + 2);
    pass = in[0] == 255 && in[2] == 0 && out[0] == 0 && out[2] == 255;
    /* coarse ASCII dump, one char per 4x4 block, top row first */
    for (int y = H - 2; y >= 0; y -= 4) {
        for (int x = 2; x < W; x += 4) {
            unsigned char *c = px + 4 * (y * W + x);
            putchar(c[0] > 128 ? '#' : '.');
        }
        putchar('\n');
    }
    printf("%s\n", pass ? "PASS" : "FAIL: unexpected colours");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    return !pass;
}
