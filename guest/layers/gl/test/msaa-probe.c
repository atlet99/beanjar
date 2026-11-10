// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Can virgl give QtWebKit's tiles real 4x MSAA without GL_EXT_multisampled_render_to_texture?
 *
 * Emulating GL_IMG_multisampled_render_to_texture with plain GL means, per tile paint: load the
 * tile's texture into a 4x renderbuffer ("unresolve": a textured quad, since GLES forbids a blit
 * into a multisampled framebuffer), paint, and resolve back with glBlitFramebuffer. This probe
 * measures, in the guest, what that costs and whether the resolve anti-aliases:
 *
 *   - the ES2 context QtWebKit uses: its extensions (is there any MSAA path at all?);
 *   - an ES3 context: GL_MAX_SAMPLES, then for SIZE x SIZE (default 256, WebKit's tile size):
 *       plain   clear + triangle into the single-sampled texture (today's path)
 *       msaa    clear + triangle into the 4x renderbuffer + resolve blit
 *       full    unresolve quad + triangle + resolve (what a partial tile update would need)
 *     each ITER times, with a glFinish after every paint (latency; virgl's round trip dominates)
 *     and after all of them (throughput), and the number of edge pixels that are neither pure
 *     triangle nor pure background after the resolve (0 = no anti-aliasing).
 *
 *   msaa-probe [SIZE [ITER]]      (surfaceless EGL; built by guest/layers/gl/test/meson.build)
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void die(const char *m)
{
    fprintf(stderr, "FAIL: %s (egl 0x%x gl 0x%x)\n", m, eglGetError(), glGetError());
    exit(1);
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
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

static GLuint program(const char *vs, const char *fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "pos");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
        die("link");
    return p;
}

static EGLContext context(EGLDisplay dpy, EGLConfig cfg, int version)
{
    const EGLint attr[] = {EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
        die("context");
    return ctx;
}

static void print_msaa_extensions(void)
{
    const char *ext = (const char *)glGetString(GL_EXTENSIONS), *p = ext;
    printf("  GL_VERSION=%s\n  MSAA-related extensions:", glGetString(GL_VERSION));
    while (p && *p) {
        const char *e = strchr(p, ' ');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if ((memmem(p, n, "ultisample", 10) || memmem(p, n, "blit", 4)) && n)
            printf(" %.*s", (int)n, p);
        p = e ? e + 1 : NULL;
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    int size = argc > 1 ? atoi(argv[1]) : 256, iter = argc > 2 ? atoi(argv[2]) : 200;
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint maj, min, n;
    if (!eglInitialize(dpy, &maj, &min))
        die("eglInitialize");
    const EGLint cattr[] = {
        EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1)
        die("eglChooseConfig");
    eglBindAPI(EGL_OPENGL_ES_API);

    printf("ES2 context (QtWebKit's):\n");
    EGLContext c2 = context(dpy, cfg, 2);
    printf("  GL_RENDERER=%s\n", glGetString(GL_RENDERER));
    print_msaa_extensions();
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, c2);

    printf("ES3 context:\n");
    context(dpy, cfg, 3);
    print_msaa_extensions();
    GLint max_samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    printf("  GL_MAX_SAMPLES=%d\n", max_samples);
    if (max_samples < 4) {
        printf("RESULT: no 4x MSAA renderbuffers\n");
        return 0;
    }

    /* Tile texture + FBO (single-sampled), and a 4x renderbuffer + FBO. */
    GLuint tex, fbo_tex, rb, fbo_ms;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_tex);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        die("texture fbo");
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, size, size);
    glGenFramebuffers(1, &fbo_ms);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_ms);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        die("msaa fbo");
    GLint samples = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);
    printf("  4x renderbuffer: %d samples, %dx%d\n", samples, size, size);

    GLuint solid = program(
        "attribute vec2 pos; void main() { gl_Position = vec4(pos, 0.0, 1.0); }",
        "precision mediump float; void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }");
    GLuint copy = program("attribute vec2 pos; varying vec2 uv; void main() { uv = pos * 0.5 + "
                          "0.5; gl_Position = vec4(pos, 0.0, 1.0); }",
                          "precision mediump float; uniform sampler2D t; varying vec2 uv; void "
                          "main() { gl_FragColor = texture2D(t, uv); }");
    /* A thin sliver: long, nearly horizontal edges cross many pixels partially. */
    static const GLfloat tri[] = {-0.9f, -0.8f, 0.9f, -0.6f, -0.9f, 0.7f};
    static const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glViewport(0, 0, size, size);
    glEnableVertexAttribArray(0);

    unsigned char *px = malloc((size_t)size * size * 4);
    const char *mode_names[] = {"plain", "msaa", "full"};
    for (int run = 0; run < 6; ++run) {
        int mode = run % 3, batch = run >= 3;
        glFinish();
        double t0 = now_ms();
        for (int i = 0; i < iter; ++i) {
            if (mode == 2) { /* unresolve: the tile's current contents into the samples */
                glBindFramebuffer(GL_FRAMEBUFFER, fbo_ms);
                glUseProgram(copy);
                glBindTexture(GL_TEXTURE_2D, tex);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, mode ? fbo_ms : fbo_tex);
            }
            glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
            if (mode != 2)
                glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(solid);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            if (mode) { /* resolve into the tile texture */
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_ms);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_tex);
                glBlitFramebuffer(0, 0, size, size, 0, 0, size, size, GL_COLOR_BUFFER_BIT,
                                  GL_NEAREST);
            }
            if (!batch)
                glFinish();
        }
        glFinish();
        double ms = (now_ms() - t0) / iter;
        /* For "full" the first iteration starts from the previous mode's picture; clear the
         * texture blue before measuring so its edges come from this mode only. */
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_tex);
        glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, px);
        long partial = 0;
        for (long i = 0; i < (long)size * size; ++i) {
            unsigned char r = px[4 * i], b = px[4 * i + 2];
            if (!((r == 255 && b == 0) || (r == 0 && b == 255)))
                ++partial;
        }
        printf("  %-5s %-10s %7.3f ms per tile paint, %ld partially covered pixels, err 0x%x\n",
               mode_names[mode], batch ? "throughput" : "latency", ms, partial, glGetError());
        if (mode != 2) { /* reset the texture to background before the next run */
            glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    free(px);
    return 0;
}
