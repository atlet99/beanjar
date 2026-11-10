// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Integer vertex attributes through virgl: feed a known value per vertex, read it back.
 *
 *   int-attribs [-v]
 *
 * For each integer type a GLES 3 client can give glVertexAttribIPointer (unsigned and signed 8,
 * 16 and 32 bits, two components) and each draw (glDrawArrays, and glDrawElements from an index
 * buffer), it draws a quad whose vertex shader reads the attribute as uvec2 or ivec2 and passes it
 * flat to the fragment shader, which writes its low bytes and whether it equals the value the
 * client sent, then reads the FBO back. A float attribute fed the same way with
 * glVertexAttribPointer is the control. Skia's glyph atlas coordinates (Flutter text) take this
 * path: `in uvec2` from glVertexAttribIPointer(GL_UNSIGNED_SHORT). Exit 0 when every case matches.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 16
#define H 16

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
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "%s\n", log);
        die("compile");
    }
    return s;
}

/* kind: "u" uvec2, "i" ivec2, "" vec2 (the float control) */
static GLuint program(const char *kind)
{
    char vs[512], fs[1024];
    const char *vt = *kind == 'u' ? "uvec2" : *kind == 'i' ? "ivec2" : "vec2";
    snprintf(vs, sizeof vs,
             "#version 300 es\n"
             "in vec2 pos; in %s a; flat out %s v;\n"
             "void main() { v = a; gl_Position = vec4(pos, 0.0, 1.0); }\n",
             vt, vt);
    snprintf(fs, sizeof fs,
             "#version 300 es\n"
             "precision highp float; precision highp int;\n"
             "flat in %s v; uniform %s want; out vec4 o;\n"
             "void main() {\n"
             "  uvec2 u = uvec2(%s);\n"
             "  o = vec4(float(u.x & 0xFFu), float((u.x >> 8) & 0xFFu), float(u.y & 0xFFu),\n"
             "           v == want ? 255.0 : 0.0) / 255.0;\n"
             "}\n",
             vt, vt, *kind ? "v" : "ivec2(round(v))");
    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "pos");
    glBindAttribLocation(p, 1, "a");
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
        die("link");
    return p;
}

struct type {
    const char *name;
    GLenum type;
    int bytes;
    const char *kind; /* program */
    int ipointer;     /* glVertexAttribIPointer, else glVertexAttribPointer (not normalized) */
    int32_t x, y;
};

static const struct type types[] = {
    {"float/u16", GL_UNSIGNED_SHORT, 2, "", 0, 1234, 40000},
    {"uvec2/u8", GL_UNSIGNED_BYTE, 1, "u", 1, 200, 77},
    {"uvec2/u16", GL_UNSIGNED_SHORT, 2, "u", 1, 1234, 40000},
    {"uvec2/u32", GL_UNSIGNED_INT, 4, "u", 1, 1234, 100000},
    {"ivec2/s8", GL_BYTE, 1, "i", 1, -100, 77},
    {"ivec2/s16", GL_SHORT, 2, "i", 1, -1234, 31000},
    {"ivec2/s32", GL_INT, 4, "i", 1, -70000, 100000},
};

/* vertex: float pos[2], then the attribute's two components at offset 8; stride 16 */
static void put(unsigned char *p, int bytes, int32_t v)
{
    if (bytes == 1)
        *p = (unsigned char)v;
    else if (bytes == 2) {
        uint16_t s = (uint16_t)v;
        memcpy(p, &s, 2);
    } else
        memcpy(p, &v, 4);
}

static int run(const struct type *t, int indexed, GLuint vbo, GLuint ebo)
{
    static const float pos[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    unsigned char verts[4][16];
    memset(verts, 0, sizeof verts);
    for (int i = 0; i < 4; i++) {
        memcpy(verts[i], pos[i], 8);
        put(verts[i] + 8, t->bytes, t->x);
        put(verts[i] + 8 + t->bytes, t->bytes, t->y);
    }
    GLuint p = program(t->kind);
    glUseProgram(p);
    GLint want = glGetUniformLocation(p, "want");
    if (*t->kind == 'u')
        glUniform2ui(want, (GLuint)t->x, (GLuint)t->y);
    else if (*t->kind == 'i')
        glUniform2i(want, t->x, t->y);
    else
        glUniform2f(want, (float)t->x, (float)t->y);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
    if (t->ipointer)
        glVertexAttribIPointer(1, 2, t->type, 16, (void *)8);
    else
        glVertexAttribPointer(1, 2, t->type, GL_FALSE, 16, (void *)8);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glClearColor(1, 0, 1, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    if (indexed) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void *)0);
    } else
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    GLenum err = glGetError();

    static unsigned char px[W * H * 4];
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    uint32_t ux = (uint32_t)t->x, uy = (uint32_t)t->y;
    const unsigned char exp[4] = {ux & 0xFF, (ux >> 8) & 0xFF, uy & 0xFF, 255};
    int bad = 0, first = -1;
    for (int i = 0; i < W * H; i++)
        if (memcmp(px + i * 4, exp, 4)) {
            if (first < 0)
                first = i;
            bad++;
        }
    printf("%-4s %-10s %-8s", bad || err ? "FAIL" : "ok", t->name, indexed ? "elements" : "arrays");
    if (err)
        printf(" gl error 0x%x", err);
    if (bad) {
        const unsigned char *g = px + first * 4;
        printf(" %d/%d texels wrong, first got %u,%u,%u,%u want %u,%u,%u,%u", bad, W * H, g[0],
               g[1], g[2], g[3], exp[0], exp[1], exp[2], exp[3]);
        if (g[0] == 255 && g[1] == 0 && g[2] == 255 && g[3] == 0)
            printf(" (not drawn)");
    }
    printf("\n");
    glDeleteProgram(p);
    return bad || err;
}

int main(int argc, char **argv)
{
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL)
                         : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj, min, n;
    if (!eglInitialize(dpy, &maj, &min))
        die("eglInitialize");
    const EGLint cattr[] = {EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                            EGL_NONE};
    EGLConfig cfg;
    if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1)
        die("eglChooseConfig");
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ctxattr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
        die("context");
    if (verbose)
        printf("GL_RENDERER %s, GL_VERSION %s\n", glGetString(GL_RENDERER),
               glGetString(GL_VERSION));

    GLuint rt, fbo, vao, vbo, ebo;
    glGenTextures(1, &rt);
    glBindTexture(GL_TEXTURE_2D, rt);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        die("fbo");
    glViewport(0, 0, W, H);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    static const GLushort idx[6] = {0, 1, 2, 2, 1, 3};
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);

    int failed = 0, total = 0;
    for (unsigned i = 0; i < sizeof types / sizeof *types; i++)
        for (int indexed = 0; indexed < 2; indexed++, total++)
            failed += run(&types[i], indexed, vbo, ebo);
    printf("%s: %d of %d cases wrong\n", failed ? "FAIL" : "PASS", failed, total);
    eglTerminate(dpy);
    return failed != 0;
}
