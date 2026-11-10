// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* beanjar-vnc's tile comparison on the GPU (tilediff.h). */
#include "tilediff.h"
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>

/* Two passes, so that each fragment reads little (one fragment a tile reading its 256 pixel
 * pairs was slow on V3D: few fragments, long dependent loops): one fragment a BLOCK-square block
 * marks whether any of its pixels differ, then one fragment a tile reads its blocks' marks. */
#define BLOCK 4
struct tilediff {
    int width, height, bw, bh, tw, th;
    GLuint program, reduce, tex, fbo, btex, bfbo;
    GLint u_cur, u_prev, u_texel, u_last, u_marks, u_btexel, u_blast;
    unsigned char *rgba;
};

static GLuint shader(GLenum type, const char *source)
{
    GLuint obj = glCreateShader(type);
    glShaderSource(obj, 1, &source, NULL);
    glCompileShader(obj);
    GLint ok;
    glGetShaderiv(obj, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(obj, sizeof log, NULL, log);
        fprintf(stderr, "beanjar-vnc: tile comparison shader: %s\n", log);
        abort();
    }
    return obj;
}

static GLuint program(const char *fs)
{
    static const char vs[] = "attribute vec2 pos; void main(){gl_Position=vec4(pos,0.,1.);}";
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "pos");
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

/* A w x h RGBA target. */
static void target(GLuint *tex, GLuint *fbo, int w, int h)
{
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
}

#define PRECISION                                                                                  \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\n"                           \
    "precision mediump float;\n#endif\n"

struct tilediff *tilediff_new(int width, int height)
{
    /* a block: any of its pixels different in the two frames. GLES 2: constant loops. */
    static const char blocks[] =
        PRECISION "uniform sampler2D cur, prev; uniform vec2 texel, last;\n"
                  "void main(){\n"
                  "  vec2 base = floor(gl_FragCoord.xy) * 4.0;\n"
                  "  vec4 d = vec4(0.0);\n"
                  "  for (int y = 0; y < 4; y++)\n"
                  "    for (int x = 0; x < 4; x++) {\n"
                  "      vec2 t = (min(base + vec2(float(x), float(y)), last) + 0.5) * texel;\n"
                  "      d += abs(texture2D(cur, t) - texture2D(prev, t));\n"
                  "    }\n"
                  "  gl_FragColor = vec4(dot(d.rgb, vec3(1.0)) > 0.0 ? 1.0 : 0.0);\n"
                  "}\n";
    /* a tile: any of its blocks marked */
    static const char tiles[] = PRECISION
        "uniform sampler2D marks; uniform vec2 btexel, blast;\n"
        "void main(){\n"
        "  vec2 base = floor(gl_FragCoord.xy) * 4.0;\n"
        "  float m = 0.0;\n"
        "  for (int y = 0; y < 4; y++)\n"
        "    for (int x = 0; x < 4; x++)\n"
        "      m = max(m, texture2D(marks, (min(base + vec2(float(x), float(y)), blast) + 0.5)"
        " * btexel).r);\n"
        "  gl_FragColor = vec4(m);\n"
        "}\n";
    struct tilediff *d = calloc(1, sizeof *d);
    d->width = width;
    d->height = height;
    d->program = program(blocks);
    d->u_cur = glGetUniformLocation(d->program, "cur");
    d->u_prev = glGetUniformLocation(d->program, "prev");
    d->u_texel = glGetUniformLocation(d->program, "texel");
    d->u_last = glGetUniformLocation(d->program, "last");
    d->reduce = program(tiles);
    d->u_marks = glGetUniformLocation(d->reduce, "marks");
    d->u_btexel = glGetUniformLocation(d->reduce, "btexel");
    d->u_blast = glGetUniformLocation(d->reduce, "blast");
    d->bw = (width + BLOCK - 1) / BLOCK;
    d->bh = (height + BLOCK - 1) / BLOCK;
    d->tw = (width + TILEDIFF_TILE - 1) / TILEDIFF_TILE;
    d->th = (height + TILEDIFF_TILE - 1) / TILEDIFF_TILE;
    d->rgba = malloc((size_t)d->tw * d->th * 4);
    GLint previous;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    target(&d->btex, &d->bfbo, d->bw, d->bh);
    target(&d->tex, &d->fbo, d->tw, d->th);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
    return d;
}

void tilediff_run(struct tilediff *d, unsigned cur, unsigned prev, int tex_h, int *tw, int *th)
{
    static const GLfloat xy[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    GLint previous, program;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glBindFramebuffer(GL_FRAMEBUFFER, d->bfbo);
    glViewport(0, 0, d->bw, d->bh);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(d->program);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, prev);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cur);
    glUniform1i(d->u_cur, 0);
    glUniform1i(d->u_prev, 1);
    glUniform2f(d->u_texel, 1.0f / (float)d->width, 1.0f / (float)tex_h);
    glUniform2f(d->u_last, (float)(d->width - 1), (float)(d->height - 1));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, xy);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    /* the frames unbound: a buffer left bound as a texture is still in use when rendered into */
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
    glViewport(0, 0, d->tw, d->th);
    glUseProgram(d->reduce);
    glBindTexture(GL_TEXTURE_2D, d->btex);
    glUniform1i(d->u_marks, 0);
    glUniform2f(d->u_btexel, 1.0f / (float)d->bw, 1.0f / (float)d->bh);
    glUniform2f(d->u_blast, (float)(d->bw - 1), (float)(d->bh - 1));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram((GLuint)program);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
    *tw = d->tw;
    *th = d->th;
}

void tilediff_read(struct tilediff *d, unsigned char *tiles)
{
    GLint previous;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, d->tw, d->th, GL_RGBA, GL_UNSIGNED_BYTE, d->rgba);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
    for (int i = 0; i < d->tw * d->th; i++)
        tiles[i] = d->rgba[(size_t)i * 4];
}

void tilediff_free(struct tilediff *d)
{
    if (!d)
        return;
    glDeleteProgram(d->program);
    glDeleteProgram(d->reduce);
    glDeleteFramebuffers(1, &d->fbo);
    glDeleteTextures(1, &d->tex);
    glDeleteFramebuffers(1, &d->bfbo);
    glDeleteTextures(1, &d->btex);
    free(d->rgba);
    free(d);
}
