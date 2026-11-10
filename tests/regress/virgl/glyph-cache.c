// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The firmware's Qt Quick 5.0 distance-field glyph cache, reduced to its GL calls.
 *
 *   glyph-cache [WIDTH]
 *
 * QSGDefaultDistanceFieldGlyphCache (libQt5Quick 5.0.0) keeps the glyphs of a font in one GL_ALPHA
 * texture of a fixed width whose height grows:
 *   - createTexture: glTexImage2D(GL_ALPHA, w, h) with a zeroed buffer;
 *   - storeGlyphs: glTexSubImage2D(GL_ALPHA) per glyph, rows 4-byte aligned (QImage), the
 *     default GL_UNPACK_ALIGNMENT of 4 (libQt5Quick never calls glPixelStorei);
 *   - resizeTexture: a new, taller GL_ALPHA texture; the old one is drawn with a plain texture2D
 *     shader into a temporary GL_RGBA texture attached to an FBO, and glCopyTexSubImage2D copies
 *     that FBO into the new GL_ALPHA texture (its alpha is the glyph).
 * This does the same with a known pattern and reads the atlas back after each step, drawing it
 * with a .aaaa shader into an RGBA FBO: upload, the RGBA temporary, the copy into GL_ALPHA, and an
 * upload after the resize. Each step prints its mismatching texels; exit 0 when all match. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int W = 256;
enum { GW = 37, GH = 45 }; /* an odd glyph size: rows padded to 40 bytes */

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
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "%s\n", log);
        die("compile");
    }
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

static unsigned char want(int x, int y, int g)
{
    return (unsigned char)(x * 7 + y * 13 + g * 29 + 1);
}

/* glyph g's texels, at (gx, gy) of the atlas */
struct glyph {
    int x, y;
};
static struct glyph glyphs[64];
static int nglyphs;

static void upload_glyph(int g, int x, int y)
{
    int stride = (GW + 3) & ~3;
    unsigned char *buf = calloc(stride, GH);
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++)
            buf[j * stride + i] = want(i, j, g);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, GW, GH, GL_ALPHA, GL_UNSIGNED_BYTE, buf);
    free(buf);
    glyphs[g] = (struct glyph){x, y};
}

static void nearest(void)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint alpha_texture(int w, int h)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    nearest();
    unsigned char *zero = calloc(w, h);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, zero);
    free(zero);
    return t;
}

static GLuint fbo, prog_alpha, prog_plain;
static const GLfloat quad[] = {-1, -1, 1, -1, 1, 1, -1, 1};

static void draw(GLuint prog, GLuint tex, int w, int h)
{
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(prog, "t"), 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

/* compare channel ch of an RGBA readback of a w x h target with the glyphs below height h */
static int check(const char *step, const unsigned char *px, int w, int h, int ch)
{
    int bad = 0, total = 0, first = 1;
    for (int g = 0; g < nglyphs; g++) {
        if (glyphs[g].y + GH > h)
            continue;
        int gbad = 0;
        for (int j = 0; j < GH; j++)
            for (int i = 0; i < GW; i++) {
                unsigned char got = px[4 * ((glyphs[g].y + j) * w + glyphs[g].x + i) + ch],
                              exp = want(i, j, g);
                total++;
                if (got != exp) {
                    bad++;
                    gbad++;
                    if (first) {
                        printf("  %s: first mismatch glyph %d texel (%d,%d): got %u want %u\n",
                               step, g, i, j, got, exp);
                        first = 0;
                    }
                }
            }
        if (gbad)
            printf("  %s: glyph %d at %d,%d: %d/%d texels wrong\n", step, g, glyphs[g].x,
                   glyphs[g].y, gbad, GW * GH);
    }
    printf("%s %s: %d of %d texels wrong\n", bad ? "FAIL" : "PASS", step, bad, total);
    return bad != 0;
}

/* the atlas drawn with .aaaa into an RGBA texture, read back */
static int verify(const char *step, GLuint tex, int h)
{
    GLuint rt;
    glGenTextures(1, &rt);
    glBindTexture(GL_TEXTURE_2D, rt);
    nearest();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        die("verify fbo");
    glClearColor(1, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    draw(prog_alpha, tex, W, h);
    unsigned char *px = malloc(4 * W * h);
    glReadPixels(0, 0, W, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int r = check(step, px, W, h, 0);
    free(px);
    glDeleteTextures(1, &rt);
    return r;
}

/* resizeTexture of Qt 5.0: returns the new texture */
static GLuint resize(GLuint old, int oldh, int newh, int *bad)
{
    GLuint t = alpha_texture(W, newh);
    GLuint tmp;
    glGenTextures(1, &tmp);
    glBindTexture(GL_TEXTURE_2D, tmp);
    nearest();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, oldh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tmp, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        die("resize fbo");
    draw(prog_plain, old, W, oldh);

    /* what the temporary holds: the old atlas in alpha */
    unsigned char *px = malloc(4 * W * oldh);
    glReadPixels(0, 0, W, oldh, GL_RGBA, GL_UNSIGNED_BYTE, px);
    char step[64];
    snprintf(step, sizeof step, "resize %d->%d: RGBA temporary .a", oldh, newh);
    *bad |= check(step, px, W, oldh, 3);
    free(px);

    glBindTexture(GL_TEXTURE_2D, t);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, W, oldh);
    if (glGetError())
        die("glCopyTexSubImage2D");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glDeleteTextures(1, &tmp);
    glDeleteTextures(1, &old);
    snprintf(step, sizeof step, "resize %d->%d: copied into the new GL_ALPHA", oldh, newh);
    *bad |= verify(step, t, newh);
    return t;
}

int main(int argc, char **argv)
{
    if (argc > 1)
        W = atoi(argv[1]);
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
    printf("GL_RENDERER=%s\nGL_VERSION=%s\natlas width %d, glyphs %dx%d\n",
           glGetString(GL_RENDERER), glGetString(GL_VERSION), W, GW, GH);

    glGenFramebuffers(1, &fbo);
    prog_alpha = program("precision mediump float; varying vec2 uv; uniform sampler2D t;\n"
                         "void main() { gl_FragColor = texture2D(t, uv).aaaa; }");
    prog_plain = program("precision mediump float; varying vec2 uv; uniform sampler2D t;\n"
                         "void main() { gl_FragColor = texture2D(t, uv); }");

    int bad = 0, h = 64, per_row = W / (GW + 1);
    GLuint atlas = alpha_texture(W, h);
    /* first row, uploaded one glyph at a time */
    for (int i = 0; i < per_row; i++)
        upload_glyph(nglyphs++, i * (GW + 1), 1);
    bad |= verify("upload into 64 rows", atlas, h);
    /* two more rows, each after a resize like the cache's */
    for (int row = 1; row < 3; row++) {
        atlas = resize(atlas, h, h + 64, &bad);
        h += 64;
        glBindTexture(GL_TEXTURE_2D, atlas);
        for (int i = 0; i < per_row; i++)
            upload_glyph(nglyphs++, i * (GW + 1), row * 64 + 1);
        char step[64];
        snprintf(step, sizeof step, "upload into %d rows after the resize", h);
        bad |= verify(step, atlas, h);
    }
    printf("%s\n", bad ? "FAIL" : "PASS");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    return bad;
}
