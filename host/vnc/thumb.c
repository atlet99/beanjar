// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* beanjar-vnc's thumbnails (thumb.h). */
#include "thumb.h"
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>

struct thumb {
    GLuint program, tex, fbo;
    GLint u_src, u_step;
    int w, h;
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
        fprintf(stderr, "beanjar-vnc: thumbnail shader: %s\n", log);
        abort();
    }
    return obj;
}

struct thumb *thumb_new(void)
{
    /* the output's row 0 (gl_FragCoord.y 0, read back first) samples the source's top row */
    static const char vs[] =
        "attribute vec2 pos; varying vec2 uv;\n"
        "void main(){ uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0., 1.); }";
    /* 4x4 bilinear samples spread over the output pixel's footprint (step: a quarter of it) */
    static const char fs[] =
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\n"
        "precision mediump float;\n#endif\n"
        "uniform sampler2D src; uniform vec2 step; varying vec2 uv;\n"
        "void main(){\n"
        "  vec4 c = vec4(0.0);\n"
        "  for (int y = 0; y < 4; y++)\n"
        "    for (int x = 0; x < 4; x++)\n"
        "      c += texture2D(src, uv + (vec2(float(x), float(y)) - 1.5) * step);\n"
        "  gl_FragColor = vec4(c.rgb / 16.0, 1.0);\n"
        "}\n";
    struct thumb *t = calloc(1, sizeof *t);
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    t->program = glCreateProgram();
    glAttachShader(t->program, v);
    glAttachShader(t->program, f);
    glBindAttribLocation(t->program, 0, "pos");
    glLinkProgram(t->program);
    glDeleteShader(v);
    glDeleteShader(f);
    t->u_src = glGetUniformLocation(t->program, "src");
    t->u_step = glGetUniformLocation(t->program, "step");
    return t;
}

/* the output target at w x h */
static int target(struct thumb *t, int w, int h)
{
    if (t->w == w && t->h == h)
        return 0;
    glDeleteFramebuffers(1, &t->fbo);
    glDeleteTextures(1, &t->tex);
    free(t->rgba);
    glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &t->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
    t->rgba = malloc((size_t)w * h * 4);
    t->w = w;
    t->h = h;
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? 0 : -1;
}

int thumb_draw(struct thumb *t, unsigned src, int sw, int sh, int ow, int oh, unsigned char *rgb)
{
    static const GLfloat xy[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    GLint previous, program, min, mag;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    int result = target(t, ow, oh);
    if (!result) {
        /* letterboxed: the source's aspect kept, centred */
        int rw = ow, rh = (int)((long long)ow * sh / sw);
        if (rh > oh) {
            rh = oh;
            rw = (int)((long long)oh * sw / sh);
        }
        rw = rw < 1 ? 1 : rw;
        rh = rh < 1 ? 1 : rh;
        glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, ow, oh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport((ow - rw) / 2, (oh - rh) / 2, rw, rh);
        glUseProgram(t->program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glUniform1i(t->u_src, 0);
        glUniform2f(t->u_step, 0.25f / (float)rw, 0.25f / (float)rh);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, xy);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
        glBindTexture(GL_TEXTURE_2D, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, ow, oh, GL_RGBA, GL_UNSIGNED_BYTE, t->rgba);
        if (glGetError() != GL_NO_ERROR)
            result = -1;
        for (size_t i = 0; i < (size_t)ow * oh; i++) {
            rgb[i * 3] = t->rgba[i * 4];
            rgb[i * 3 + 1] = t->rgba[i * 4 + 1];
            rgb[i * 3 + 2] = t->rgba[i * 4 + 2];
        }
    }
    glUseProgram((GLuint)program);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
    return result;
}

void thumb_free(struct thumb *t)
{
    if (!t)
        return;
    glDeleteProgram(t->program);
    glDeleteFramebuffers(1, &t->fbo);
    glDeleteTextures(1, &t->tex);
    free(t->rgba);
    free(t);
}
