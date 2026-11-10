// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* beanjar-vnc's RGB to NV12 on the GPU (yuv.h). GLES 2 (the context main.c makes), so no integer
 * texel fetches: each texel is sampled at its centre with nearest filtering. */
#include "yuv.h"
#include <GLES2/gl2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct yuv {
    GLuint program, tex, fbo;
    GLint u_rgb, u_texel, u_last, u_rows;
    int w, h; /* the target's: yuv_stride / 4 x height * 3 / 2 */
};

/* One fragment: four bytes of a row of the NV12 picture. Rows below "rows" are Y (pixels x .. x+3),
 * the rest U, V of two chroma samples. "last": the picture's last pixel (the edge repeated). */
static const char vs[] = "attribute vec2 pos; void main(){gl_Position=vec4(pos,0.,1.);}";
static const char fs[] =
    "precision highp float;\n"
    "uniform sampler2D rgb; uniform vec2 texel, last; uniform float rows;\n"
    "vec3 at(float x, float y){\n"
    "  return texture2D(rgb, (clamp(vec2(x, y), vec2(0.0), last) + 0.5) * texel).rgb;\n"
    "}\n"
    "float q(float z){ return floor(z + 0.5) / 255.0; }\n"
    "float luma(float x, float y){\n"
    "  return q(16.0 + dot(at(x, y), vec3(65.481, 128.553, 24.966)));\n"
    "}\n"
    "vec2 chroma(float cx, float cy){\n"
    "  float x = cx * 2.0, y = cy * 2.0;\n"
    "  vec3 c = ((at(x, y - 1.0) + at(x + 1.0, y - 1.0) + at(x, y + 2.0) + at(x + 1.0, y + 2.0))\n"
    "            * 0.125 + (at(x, y) + at(x + 1.0, y) + at(x, y + 1.0) + at(x + 1.0, y + 1.0))\n"
    "            * 0.375) * 0.5;\n"
    "  return vec2(q(128.0 + dot(c, vec3(-37.797, -74.203, 112.0))),\n"
    "              q(128.0 + dot(c, vec3(112.0, -93.786, -18.214))));\n"
    "}\n"
    "void main(){\n"
    "  vec2 p = floor(gl_FragCoord.xy);\n"
    "  if (p.y < rows) {\n"
    "    float x = p.x * 4.0;\n"
    "    gl_FragColor = vec4(luma(x, p.y), luma(x + 1.0, p.y), luma(x + 2.0, p.y),\n"
    "                        luma(x + 3.0, p.y));\n"
    "  } else {\n"
    "    float cx = p.x * 2.0, cy = p.y - rows;\n"
    "    gl_FragColor = vec4(chroma(cx, cy), chroma(cx + 1.0, cy));\n"
    "  }\n"
    "}\n";

static GLuint shader(GLenum type, const char *source, const char **why)
{
    GLuint obj = glCreateShader(type);
    glShaderSource(obj, 1, &source, NULL);
    glCompileShader(obj);
    GLint ok;
    glGetShaderiv(obj, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(obj, sizeof log, NULL, log);
        fprintf(stderr, "beanjar-vnc: video: GPU conversion shader: %s\n", log);
        glDeleteShader(obj);
        *why = "its shader does not compile";
        return 0;
    }
    return obj;
}

static void target_drop(struct yuv *y)
{
    if (y->fbo)
        glDeleteFramebuffers(1, &y->fbo);
    if (y->tex)
        glDeleteTextures(1, &y->tex);
    y->fbo = y->tex = 0;
    y->w = y->h = 0;
}

/* The RGBA target for an NV12 picture of width x height. */
static int target(struct yuv *y, int width, int height)
{
    int w = yuv_stride(width) / 4, h = height * 3 / 2;
    if (y->w == w && y->h == h)
        return 0;
    target_drop(y);
    glGenTextures(1, &y->tex);
    glBindTexture(GL_TEXTURE_2D, y->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &y->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, y->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, y->tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        target_drop(y);
        return -1;
    }
    y->w = w;
    y->h = h;
    return 0;
}

int yuv_convert(struct yuv *y, unsigned tex, int tex_w, int tex_h, int width, int height,
                unsigned char *nv12)
{
    static const GLfloat xy[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    if (width < 2 || height < 2 || (width | height) & 1 || width > tex_w || height > tex_h)
        return -1;
    while (glGetError() != GL_NO_ERROR) {
    }
    GLint previous, program;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    int result = -1;
    if (!target(y, width, height)) {
        GLboolean dither = glIsEnabled(GL_DITHER);
        glBindFramebuffer(GL_FRAMEBUFFER, y->fbo);
        glViewport(0, 0, y->w, y->h);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DITHER);
        glUseProgram(y->program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(y->u_rgb, 0);
        glUniform2f(y->u_texel, 1.0f / (float)tex_w, 1.0f / (float)tex_h);
        glUniform2f(y->u_last, (float)(width - 1), (float)(height - 1));
        glUniform1f(y->u_rows, (float)height);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, xy);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        /* unbound: a buffer left bound as a texture is still in use when rendered into */
        glBindTexture(GL_TEXTURE_2D, 0);
        if (dither)
            glEnable(GL_DITHER);
        /* rows of yuv_stride bytes, a multiple of 4: the default alignment */
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, y->w, y->h, GL_RGBA, GL_UNSIGNED_BYTE, nv12);
        result = glGetError() == GL_NO_ERROR ? 0 : -1;
    }
    glUseProgram((GLuint)program);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
    return result;
}

/* The self-check's picture (top row first) and the conversion done in double precision. */
enum { CHECK_W = 14, CHECK_H = 6, CHECK_TW = 16 };
static void check_pixel(int x, int y, unsigned char *p)
{
    static const unsigned char colours[][3] = {{255, 0, 0},   {0, 255, 0},     {0, 0, 255},
                                               {255, 255, 0}, {0, 255, 255},   {255, 0, 255},
                                               {0, 0, 0},     {255, 255, 255}, {32, 128, 192}};
    unsigned i = (unsigned)(x * 7 + y * 3 + (x * y) % 5) % 9;
    memcpy(p, colours[i], 3);
    p[3] = 255;
}
static double ref_at(const unsigned char *rgba, int x, int y, int k)
{
    x = x < 0 ? 0 : x > CHECK_W - 1 ? CHECK_W - 1 : x;
    y = y < 0 ? 0 : y > CHECK_H - 1 ? CHECK_H - 1 : y;
    return rgba[((size_t)y * CHECK_TW + x) * 4 + k] / 255.0;
}
static int ref_value(const unsigned char *rgba, int x, int y, int plane)
{
    static const double m[3][3] = {
        {65.481, 128.553, 24.966}, {-37.797, -74.203, 112.0}, {112.0, -93.786, -18.214}};
    double c[3], z = plane ? 128.0 : 16.0;
    for (int k = 0; k < 3; k++) {
        if (!plane) {
            c[k] = ref_at(rgba, x, y, k);
            continue;
        }
        int cx = x * 2, cy = y * 2;
        c[k] = ((ref_at(rgba, cx, cy - 1, k) + ref_at(rgba, cx + 1, cy - 1, k) +
                 ref_at(rgba, cx, cy + 2, k) + ref_at(rgba, cx + 1, cy + 2, k)) *
                    0.125 +
                (ref_at(rgba, cx, cy, k) + ref_at(rgba, cx + 1, cy, k) +
                 ref_at(rgba, cx, cy + 1, k) + ref_at(rgba, cx + 1, cy + 1, k)) *
                    0.375) *
               0.5;
    }
    for (int k = 0; k < 3; k++)
        z += m[plane][k] * c[k];
    return (int)floor(z + 0.5);
}
/* The largest difference from the reference, in code values; -1 if the conversion failed. */
static int self_check(struct yuv *y)
{
    unsigned char rgba[CHECK_TW * CHECK_H * 4];
    for (int j = 0; j < CHECK_H; j++)
        for (int i = 0; i < CHECK_TW; i++)
            check_pixel(i, j, rgba + ((size_t)j * CHECK_TW + i) * 4);
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CHECK_TW, CHECK_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    int stride = yuv_stride(CHECK_W);
    unsigned char out[16 * CHECK_H * 3 / 2];
    int worst = -1;
    if (!yuv_convert(y, tex, CHECK_TW, CHECK_H, CHECK_W, CHECK_H, out)) {
        worst = 0;
        for (int j = 0; j < CHECK_H; j++)
            for (int i = 0; i < CHECK_W; i++) {
                int d = abs(out[j * stride + i] - ref_value(rgba, i, j, 0));
                worst = d > worst ? d : worst;
            }
        for (int j = 0; j < CHECK_H / 2; j++)
            for (int i = 0; i < CHECK_W / 2; i++)
                for (int p = 1; p <= 2; p++) {
                    int got = out[(CHECK_H + j) * stride + i * 2 + p - 1];
                    int d = abs(got - ref_value(rgba, i, j, p));
                    worst = d > worst ? d : worst;
                }
    }
    glDeleteTextures(1, &tex);
    return worst;
}

struct yuv *yuv_new(const char **why)
{
    GLint range[2] = {0, 0}, precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    if (precision < 23) {
        *why = "no high precision floats in fragment shaders";
        return NULL;
    }
    GLuint v = shader(GL_VERTEX_SHADER, vs, why);
    GLuint f = v ? shader(GL_FRAGMENT_SHADER, fs, why) : 0;
    if (!f) {
        if (v)
            glDeleteShader(v);
        return NULL;
    }
    struct yuv *y = calloc(1, sizeof *y);
    y->program = glCreateProgram();
    glAttachShader(y->program, v);
    glAttachShader(y->program, f);
    glBindAttribLocation(y->program, 0, "pos");
    glLinkProgram(y->program);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint linked;
    glGetProgramiv(y->program, GL_LINK_STATUS, &linked);
    if (!linked) {
        *why = "its shader does not link";
        yuv_free(y);
        return NULL;
    }
    y->u_rgb = glGetUniformLocation(y->program, "rgb");
    y->u_texel = glGetUniformLocation(y->program, "texel");
    y->u_last = glGetUniformLocation(y->program, "last");
    y->u_rows = glGetUniformLocation(y->program, "rows");
    int worst = self_check(y);
    target_drop(y);
    if (worst < 0 || worst > 1) {
        *why = worst < 0 ? "it fails on a test picture" : "it differs on a test picture";
        if (worst > 1)
            fprintf(stderr, "beanjar-vnc: video: GPU conversion: %d code values off\n", worst);
        yuv_free(y);
        return NULL;
    }
    return y;
}

void yuv_free(struct yuv *y)
{
    if (!y)
        return;
    target_drop(y);
    if (y->program)
        glDeleteProgram(y->program);
    free(y);
}
