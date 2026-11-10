// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Image texture formats through virgl: upload a known pattern, draw it, read it back.
 *
 *   tex-formats [-v]
 *
 * For each format a GLES 2 client can upload images in (RGBA, BGRA_EXT, RGB, 565, 4444, 5551,
 * ALPHA, LUMINANCE, LUMINANCE_ALPHA), each upload path (glTexImage2D with the data, glTexImage2D
 * then glTexSubImage2D; a small texture whose data rides in the command stream and a large one
 * that goes through a transfer) and each sampling (a plain texture2D, and the texture drawn again
 * after an ALPHA texture was sampled on the same unit), it draws the texture into an RGBA FBO and
 * compares the readback with what GLES 2 says the texel is. The pattern is per texel and channel,
 * so a lost, swapped or constant channel shows as such: the summary says, per case, which
 * channels were wrong ("r" lost to 0, "R" another value) and the first wrong texel.
 * Exit 0 when every case matches (within 1 for 8-bit channels, 1 step for packed ones). */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

static int verbose;

static void die(const char *m)
{
    fprintf(stderr, "FAIL: %s (egl 0x%x gl 0x%x)\n", m, eglGetError(), glGetError());
    exit(2);
}

static GLuint shader(GLenum t, const char *src)
{
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
        die("compile");
    return s;
}

static GLuint program(const char *fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(
        p, shader(GL_VERTEX_SHADER,
                  "attribute vec2 pos; varying vec2 uv;\n"
                  "void main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }"));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "pos");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
        die("link");
    return p;
}

/* the reference RGBA8 of texel (x, y): the colours of the issue (Live TV yellow, YouTube red,
 * grey, white) in a corner, then gradients that differ per channel */
static void ref(int x, int y, unsigned char c[4])
{
    static const unsigned char fixed[4][4] = {
        {244, 195, 61, 255}, {228, 46, 40, 255}, {128, 128, 128, 255}, {255, 255, 255, 255}};
    if (y == 0 && x < 4) {
        memcpy(c, fixed[x], 4);
        return;
    }
    c[0] = (unsigned char)(x * 5 + y * 3 + 17);
    c[1] = (unsigned char)(x * 3 + y * 7 + 101);
    c[2] = (unsigned char)(x * 11 + y * 2 + 53);
    c[3] = (unsigned char)(255 - ((x + y) & 0x3f));
}

struct fmt {
    const char *name;
    GLenum format, type;
    int bpp;
};

static const struct fmt fmts[] = {
    {"RGBA8888", GL_RGBA, GL_UNSIGNED_BYTE, 4},
    {"BGRA8888", GL_BGRA_EXT, GL_UNSIGNED_BYTE, 4},
    {"RGB888", GL_RGB, GL_UNSIGNED_BYTE, 3},
    {"RGB565", GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2},
    {"RGBA4444", GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2},
    {"RGBA5551", GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2},
    {"ALPHA", GL_ALPHA, GL_UNSIGNED_BYTE, 1},
    {"LUMINANCE", GL_LUMINANCE, GL_UNSIGNED_BYTE, 1},
    {"LUMINANCE_ALPHA", GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, 2},
};

static unsigned q(unsigned v, int bits)
{
    return (v * ((1u << bits) - 1) + 127) / 255;
}
static unsigned char e(unsigned v, int bits)
{
    return (unsigned char)((v * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1));
}

/* pack texel (x, y) of the pattern in format f; return what GLES 2 samples from it (RGBA8),
 * and the tolerance per channel */
static void pack(const struct fmt *f, int x, int y, unsigned char *dst, unsigned char want[4],
                 int *tol)
{
    unsigned char c[4];
    ref(x, y, c);
    *tol = 1;
    switch (f->type) {
    case GL_UNSIGNED_SHORT_5_6_5: {
        unsigned r = q(c[0], 5), g = q(c[1], 6), b = q(c[2], 5);
        unsigned short v = (unsigned short)(r << 11 | g << 5 | b);
        memcpy(dst, &v, 2);
        want[0] = e(r, 5), want[1] = e(g, 6), want[2] = e(b, 5), want[3] = 255;
        *tol = 9;
        return;
    }
    case GL_UNSIGNED_SHORT_4_4_4_4: {
        unsigned r = q(c[0], 4), g = q(c[1], 4), b = q(c[2], 4), a = q(c[3], 4);
        unsigned short v = (unsigned short)(r << 12 | g << 8 | b << 4 | a);
        memcpy(dst, &v, 2);
        want[0] = e(r, 4), want[1] = e(g, 4), want[2] = e(b, 4), want[3] = e(a, 4);
        *tol = 17;
        return;
    }
    case GL_UNSIGNED_SHORT_5_5_5_1: {
        unsigned r = q(c[0], 5), g = q(c[1], 5), b = q(c[2], 5), a = c[3] >= 128;
        unsigned short v = (unsigned short)(r << 11 | g << 6 | b << 1 | a);
        memcpy(dst, &v, 2);
        want[0] = e(r, 5), want[1] = e(g, 5), want[2] = e(b, 5), want[3] = a ? 255 : 0;
        *tol = 9;
        return;
    }
    }
    switch (f->format) {
    case GL_RGBA:
        memcpy(dst, c, 4);
        memcpy(want, c, 4);
        return;
    case GL_BGRA_EXT:
        dst[0] = c[2], dst[1] = c[1], dst[2] = c[0], dst[3] = c[3];
        memcpy(want, c, 4);
        return;
    case GL_RGB:
        memcpy(dst, c, 3);
        memcpy(want, c, 3);
        want[3] = 255;
        return;
    case GL_ALPHA:
        dst[0] = c[3];
        want[0] = want[1] = want[2] = 0, want[3] = c[3];
        return;
    case GL_LUMINANCE:
        dst[0] = c[1];
        want[0] = want[1] = want[2] = c[1], want[3] = 255;
        return;
    case GL_LUMINANCE_ALPHA:
        dst[0] = c[1], dst[1] = c[3];
        want[0] = want[1] = want[2] = c[1], want[3] = c[3];
        return;
    }
}

static GLuint fbo, rt, prog_plain, prog_alpha;
static const GLfloat quad[] = {-1, -1, 1, -1, 1, 1, -1, 1};

static void nearest(void)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void draw(GLuint prog, GLuint tex, int w, int h)
{
    glViewport(0, 0, w, h);
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(prog, "t"), 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

/* run one case; return 1 when it failed */
static int run(const struct fmt *f, int size, int sub, int after_alpha)
{
    int w = size, h = size, stride = (w * f->bpp + 3) & ~3;
    unsigned char *data = calloc(stride, h), *want = malloc(4 * w * h);
    int *tol = malloc(sizeof(int) * w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            pack(f, x, y, data + y * stride + x * f->bpp, want + 4 * (y * w + x), &tol[y * w + x]);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    nearest();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (sub) {
        glTexImage2D(GL_TEXTURE_2D, 0, f->format, w, h, 0, f->format, f->type, NULL);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, f->format, f->type, data);
    } else
        glTexImage2D(GL_TEXTURE_2D, 0, f->format, w, h, 0, f->format, f->type, data);
    GLenum err = glGetError();

    glBindTexture(GL_TEXTURE_2D, rt);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        die("fbo");
    if (after_alpha) {
        /* an ALPHA texture sampled first on the same unit, as a glyph atlas is between images */
        GLuint a;
        unsigned char texel[16];
        memset(texel, 0x80, sizeof texel);
        glGenTextures(1, &a);
        glBindTexture(GL_TEXTURE_2D, a);
        nearest();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 4, 4, 0, GL_ALPHA, GL_UNSIGNED_BYTE, texel);
        draw(prog_alpha, a, w, h);
        glDeleteTextures(1, &a);
    }
    glClearColor(1, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    draw(prog_plain, tex, w, h);
    unsigned char *got = malloc(4 * w * h);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, got);
    if (!err)
        err = glGetError();

    int bad = 0, first = -1;
    int lost[4] = {0}, other[4] = {0};
    for (int i = 0; i < w * h; i++)
        for (int c = 0; c < 4; c++) {
            int d = got[4 * i + c] - want[4 * i + c];
            if (d > tol[i] || d < -tol[i]) {
                if (first < 0)
                    first = i;
                bad++;
                if (got[4 * i + c] == 0)
                    lost[c]++;
                else
                    other[c]++;
            }
        }
    char chans[9] = "", *p = chans;
    for (int c = 0; c < 4; c++)
        if (lost[c] || other[c])
            *p++ = lost[c] > other[c] ? "rgba"[c] : "RGBA"[c];
    *p = 0;
    printf("%-4s %-15s %-3dx%-3d %-8s %-11s", bad || err ? "FAIL" : "ok", f->name, w, h,
           sub ? "sub" : "image", after_alpha ? "after-alpha" : "plain");
    if (err)
        printf(" gl error 0x%x", err);
    if (bad) {
        int x = first % w, y = first / w;
        printf(" channels %s, %d values; first (%d,%d) got %u,%u,%u,%u want %u,%u,%u,%u", chans,
               bad, x, y, got[4 * first], got[4 * first + 1], got[4 * first + 2],
               got[4 * first + 3], want[4 * first], want[4 * first + 1], want[4 * first + 2],
               want[4 * first + 3]);
    }
    printf("\n");
    if (verbose && bad)
        for (int i = 0; i < 4 && i < w; i++)
            printf("     texel (%d,0) got %u,%u,%u,%u want %u,%u,%u,%u\n", i, got[4 * i],
                   got[4 * i + 1], got[4 * i + 2], got[4 * i + 3], want[4 * i], want[4 * i + 1],
                   want[4 * i + 2], want[4 * i + 3]);
    glDeleteTextures(1, &tex);
    free(data);
    free(want);
    free(tol);
    free(got);
    return bad || err;
}

int main(int argc, char **argv)
{
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL)
                         : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min))
        die("eglInitialize");
    const EGLint cattr[] = {EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1)
        die("eglChooseConfig");
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ctxattr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT)
        die("eglCreateContext");
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
        die("eglMakeCurrent");
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    int bgra = ext && strstr(ext, "GL_EXT_texture_format_BGRA8888");
    printf("GL_RENDERER=%s\nGL_VERSION=%s\nGL_EXT_texture_format_BGRA8888: %s\n",
           glGetString(GL_RENDERER), glGetString(GL_VERSION), bgra ? "yes" : "no");

    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &rt);
    glBindTexture(GL_TEXTURE_2D, rt);
    nearest();
    glDisable(GL_BLEND);
    prog_plain = program("precision mediump float; varying vec2 uv; uniform sampler2D t;\n"
                         "void main() { gl_FragColor = texture2D(t, uv); }");
    prog_alpha = program("precision mediump float; varying vec2 uv; uniform sampler2D t;\n"
                         "void main() { gl_FragColor = texture2D(t, uv).aaaa; }");

    static const int sizes[] = {16, 256};
    int failed = 0, total = 0;
    for (unsigned i = 0; i < sizeof fmts / sizeof *fmts; i++) {
        if (fmts[i].format == GL_BGRA_EXT && !bgra)
            continue;
        for (int s = 0; s < 2; s++)
            for (int sub = 0; sub < 2; sub++)
                for (int aa = 0; aa < 2; aa++) {
                    failed += run(&fmts[i], sizes[s], sub, aa);
                    total++;
                }
    }
    printf("%s: %d of %d cases wrong\n", failed ? "FAIL" : "PASS", failed, total);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    return failed != 0;
}
