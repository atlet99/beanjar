// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The host side of guest texture sampling, without a VM: does the host GL return a texture's
 * channels when it is sampled the way virglrenderer samples it?
 *
 *   host-swizzle [-es] [RENDER_NODE]
 *
 * Runs on the VM host. It makes the context the way QEMU's virgl does (EGL on the render node's
 * GBM device, default /dev/dri/renderD128, else a surfaceless display; a desktop core profile, the
 * highest of 4.6 down to 3.1 the driver gives, or with -es a GLES 3 context), uploads an RGBA8
 * texture of known texels and draws it into an RGBA8 FBO with a plain texture() shader, then
 * reads it back. Each case varies one piece of the state vrend sets around a sampler view: the
 * storage (glTexImage2D or glTexStorage2D, vrend's immutable textures), the swizzle (none; the
 * identity through glTexParameteriv(GL_TEXTURE_SWIZZLE_RGBA), vrend's call on desktop GL; the
 * identity per component, its call on GLES; the identity after the 0,0,0,R swizzle of an
 * alpha-only view) and a sampler object bound or not. Each line says which channels came back
 * wrong. Then vrend's state with fragment shaders shaped like vrend's: N colour outputs bound with
 * glBindFragDataLocation, fsout_c1.. copied from fsout_c0, one draw buffer (fs_source()).
 * Exit 0 when every case matches. Panfrost on Midgard (Mali-T860, Mesa 25.0.7) fails the 7- and
 * 8-output cases with red 0 (guest/layers/gl/patches/mesa/0004-*). Not part of a build:
 * linked against libepoxy like QEMU and virglrenderer, for the PC or, cross, for an arm64 host:
 *   cc -O2 -o host-swizzle tests/regress/virgl/host-swizzle.c -lepoxy -lgbm
 *   aarch64-linux-gnu-gcc -O2 -o host-swizzle tests/regress/virgl/host-swizzle.c \
 *       $(aarch64-linux-gnu-pkg-config --cflags --libs epoxy gbm) */
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { W = 16, H = 16 };

static int es;

static void die(const char *m)
{
    fprintf(stderr, "FAIL: %s (egl 0x%x)\n", m, eglGetError());
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

/* fragment shaders: the plain one, then the shape of vrend's (#version 140, colour outputs
 * fsout_c0..N-1 bound to locations 0..N-1, fsout_c0 the texel and the others copied from it, as
 * vrend writes a TGSI FS_COLOR0_WRITES_ALL_CBUFS shader for a host with N draw buffers), for N
 * from 2 to 8, and eight declared with only fsout_c0 written */
struct fsv {
    const char *name;
    int declared, written;
};
static const struct fsv fsvs[] = {
    {"plain", 0, 0}, {"2-out", 2, 2},         {"3-out", 3, 3},
    {"4-out", 4, 4}, {"5-out", 5, 5},         {"6-out", 6, 6},
    {"7-out", 7, 7}, {"8-out (vrend)", 8, 8}, {"8-decl-1-written", 8, 1},
};
enum { FS_N = sizeof fsvs / sizeof *fsvs };

static const char *fs_source(int f)
{
    static char src[2048];
    const struct fsv *v = &fsvs[f];
    if (!v->declared)
        return "#version 140\nin vec2 uv; out vec4 c; uniform sampler2D t;\n"
               "void main() { c = texture(t, uv); }";
    int n = snprintf(src, sizeof src, "#version 140\nin vec2 uv;\n");
    for (int i = 0; i < v->declared; i++)
        n += snprintf(src + n, sizeof src - n, "out vec4 fsout_c%d;\n", i);
    n += snprintf(src + n, sizeof src - n,
                  "uniform sampler2D t;\nvoid main(void)\n{\n"
                  "fsout_c0 = vec4((texture(t, (uv.xyyy).xy)));\n");
    for (int i = 1; i < v->written; i++)
        n += snprintf(src + n, sizeof src - n, "fsout_c%d = fsout_c0;\n", i);
    snprintf(src + n, sizeof src - n, "}\n");
    return src;
}

static GLuint program(int fsv)
{
    const char *vs_desk =
        "#version 140\nin vec2 pos; out vec2 uv;\n"
        "void main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }";
    const char *fs_desk = "#version 140\nin vec2 uv; out vec4 c; uniform sampler2D t;\n"
                          "void main() { c = texture(t, uv); }";
    const char *vs_es = "#version 300 es\nin vec2 pos; out vec2 uv;\n"
                        "void main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }";
    const char *fs_es = "#version 300 es\nprecision highp float; in vec2 uv; out vec4 c;\n"
                        "uniform sampler2D t; void main() { c = texture(t, uv); }";
    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, es ? vs_es : vs_desk));
    (void)fs_desk;
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, es ? fs_es : fs_source(fsv)));
    glBindAttribLocation(p, 0, "pos");
    if (!es)
        for (int i = 0; i < fsvs[fsv].declared; i++) {
            char name[24];
            snprintf(name, sizeof name, "fsout_c%d", i);
            glBindFragDataLocation(p, i, name);
        }
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
        die("link");
    return p;
}

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

enum swz { SWZ_NONE, SWZ_RGBA_IV, SWZ_PER_COMPONENT, SWZ_AFTER_ALPHA, SWZ_N };
static const char *swz_name[] = {"no-swizzle", "SWIZZLE_RGBA", "SWIZZLE_R..A", "after-000R"};

static GLuint progs[FS_N], prog, fbo, rt, vbo, vao;
static int cur_fs;

static void set_swizzle(const GLint s[4], int per_component)
{
    if (per_component)
        for (int i = 0; i < 4; i++)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R + i, s[i]);
    else
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, s);
}

static int run(int storage, enum swz swz, int sampler)
{
    unsigned char data[W * H * 4], got[W * H * 4];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            ref(x, y, data + 4 * (y * W + x));

    GLuint tex, smp = 0;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (storage) {
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, data);
    } else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    static const GLint identity[4] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
    static const GLint alpha[4] = {GL_ZERO, GL_ZERO, GL_ZERO, GL_RED};
    switch (swz) {
    case SWZ_NONE:
        break;
    case SWZ_RGBA_IV:
        set_swizzle(identity, es);
        break;
    case SWZ_PER_COMPONENT:
        set_swizzle(identity, 1);
        break;
    case SWZ_AFTER_ALPHA:
        set_swizzle(alpha, es);
        set_swizzle(identity, es);
        break;
    default:
        break;
    }
    if (sampler) {
        glGenSamplers(1, &smp);
        glSamplerParameteri(smp, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glSamplerParameteri(smp, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(smp, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(smp, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindSampler(0, smp);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, W, H);
    glClearColor(1, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "t"), 0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, got);
    GLenum err = glGetError();

    int bad = 0, first = -1, lost[4] = {0}, other[4] = {0};
    for (int i = 0; i < W * H; i++)
        for (int c = 0; c < 4; c++) {
            int d = got[4 * i + c] - data[4 * i + c];
            if (d > 1 || d < -1) {
                bad++;
                if (first < 0)
                    first = i;
                if (got[4 * i + c] == 0)
                    lost[c]++;
                else
                    other[c]++;
            }
        }
    char chans[5] = "", *p = chans;
    for (int c = 0; c < 4; c++)
        if (lost[c] || other[c])
            *p++ = lost[c] > other[c] ? "rgba"[c] : "RGBA"[c];
    *p = 0;
    printf("%-4s %-16s %-12s %-12s %-10s", bad || err ? "FAIL" : "ok",
           es ? "es" : fsvs[cur_fs].name, storage ? "TexStorage" : "TexImage", swz_name[swz],
           sampler ? "sampler" : "no-sampler");
    if (err)
        printf(" gl error 0x%x", err);
    if (bad)
        printf(" channels %s; (0,0) got %u,%u,%u,%u want %u,%u,%u,%u", chans, got[0], got[1],
               got[2], got[3], data[0], data[1], data[2], data[3]);
    printf("\n");
    if (smp) {
        glBindSampler(0, 0);
        glDeleteSamplers(1, &smp);
    }
    glDeleteTextures(1, &tex);
    return bad || err;
}

int main(int argc, char **argv)
{
    const char *node = "/dev/dri/renderD128";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-es"))
            es = 1;
        else
            node = argv[i];
    }
    EGLDisplay dpy = EGL_NO_DISPLAY;
    int fd = open(node, O_RDWR | O_CLOEXEC);
    struct gbm_device *gbm = fd >= 0 ? gbm_create_device(fd) : NULL;
    if (gbm)
        dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    else
        dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min))
        die("eglInitialize");
    if (!eglBindAPI(es ? EGL_OPENGL_ES_API : EGL_OPENGL_API))
        die("eglBindAPI");
    EGLContext ctx = EGL_NO_CONTEXT;
    static const int versions[][2] = {{4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1},
                                      {4, 0}, {3, 3}, {3, 2}, {3, 1}, {3, 0}};
    for (unsigned v = es ? 9 : 0; v < sizeof versions / sizeof *versions && !ctx; v++) {
        EGLint attr[] = {EGL_CONTEXT_MAJOR_VERSION,
                         versions[v][0],
                         EGL_CONTEXT_MINOR_VERSION,
                         versions[v][1],
                         es ? EGL_NONE : EGL_CONTEXT_OPENGL_PROFILE_MASK,
                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                         EGL_NONE};
        ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attr);
    }
    if (ctx == EGL_NO_CONTEXT)
        die("eglCreateContext");
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
        die("eglMakeCurrent");
    printf("EGL on %s\nGL_RENDERER=%s\nGL_VERSION=%s\n", gbm ? node : "surfaceless",
           glGetString(GL_RENDERER), glGetString(GL_VERSION));

    for (int f = 0; f < (es ? 1 : FS_N); f++)
        progs[f] = program(f);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    static const GLfloat quad[] = {-1, -1, 1, -1, 1, 1, -1, 1};
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glGenTextures(1, &rt);
    glBindTexture(GL_TEXTURE_2D, rt);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        die("fbo");

    static const GLenum one[] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, one);
    int failed = 0, total = 0;
    prog = progs[cur_fs = 0];
    for (int storage = 0; storage < 2; storage++)
        for (int s = 0; s < SWZ_N; s++)
            for (int sampler = 0; sampler < 2; sampler++) {
                failed += run(storage, s, sampler);
                total++;
            }
    /* vrend's state (immutable storage, the swizzle set, a sampler object) with each shader */
    for (int f = 1; f < (es ? 1 : FS_N); f++) {
        prog = progs[cur_fs = f];
        failed += run(1, SWZ_RGBA_IV, 1);
        failed += run(0, SWZ_NONE, 0);
        total += 2;
    }
    printf("%s: %d of %d cases wrong\n", failed ? "FAIL" : "PASS", failed, total);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    return failed != 0;
}
