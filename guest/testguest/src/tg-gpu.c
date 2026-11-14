// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The test guest's GPU check: an EGL context on the virtio-gpu render node through GBM, a known
 * colour drawn into an offscreen framebuffer and read back, the GL renderer string and a frame
 * time.
 *
 *   tg-gpu [render node]      (default /dev/dri/renderD128)
 *
 * Prints key=value lines (gpu.renderer, gpu.pixel, gpu.frame_ms, ...) and exits 0 when the pixel
 * read back is the colour drawn. Nothing is shown on the display: the check is of the 3D path
 * (Mesa's virgl driver, virtio-gpu, the host's virglrenderer), not of scan-out. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define W      256
#define H      256
#define FRAMES 100

static const char *vs_src = "attribute vec2 pos;\n"
                            "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs_src = "precision mediump float;\n"
                            "uniform vec4 colour;\n"
                            "void main() { gl_FragColor = colour; }\n";

static int fail(const char *what)
{
    printf("gpu.error=%s (EGL 0x%04x, GL 0x%04x)\n", what, eglGetError(), glGetError());
    return 1;
}

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    return ok ? s : 0;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
    int fd = open(node, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        printf("gpu.error=open %s failed\n", node);
        return 1;
    }
    struct gbm_device *gbm = gbm_create_device(fd);
    if (!gbm)
        return fail("gbm_create_device");
    printf("gpu.gbm_backend=%s\n", gbm_device_get_backend_name(gbm));

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!get_display)
        return fail("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = get_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    EGLint major, minor;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor))
        return fail("eglInitialize");
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        return fail("eglBindAPI");
    const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT)
        return fail("eglCreateContext");
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
        return fail("eglMakeCurrent (surfaceless)");

    printf("gpu.egl_version=%d.%d\n", major, minor);
    printf("gpu.vendor=%s\n", (const char *)glGetString(GL_VENDOR));
    printf("gpu.renderer=%s\n", (const char *)glGetString(GL_RENDERER));
    printf("gpu.gl_version=%s\n", (const char *)glGetString(GL_VERSION));

    GLuint rb, fb;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, W, H);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return fail("framebuffer incomplete");

    GLuint vs = shader(GL_VERTEX_SHADER, vs_src), fs = shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs)
        return fail("shader compile");
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked)
        return fail("program link");
    glUseProgram(prog);
    static const GLfloat tri[] = {-1, -1, 3, -1, -1, 3};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
    glEnableVertexAttribArray(0);
    GLint colour = glGetUniformLocation(prog, "colour");
    glViewport(0, 0, W, H);

    /* Cleared to blue, then a triangle covering the target in a colour that 4 bits a channel
     * hold exactly (0x33/0x99/0xcc), so the read back must match to the byte. */
    glClearColor(0, 0, 1, 1);
    double t0 = now_ms();
    for (int i = 0; i < FRAMES; i++) {
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform4f(colour, 0x33 / 255.0f, 0x99 / 255.0f, 0xcc / 255.0f, 1);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
    }
    double frame_ms = (now_ms() - t0) / FRAMES;

    unsigned char px[4] = {0};
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("gpu.pixel=%02x%02x%02x%02x\n", px[0], px[1], px[2], px[3]);
    printf("gpu.expected=3399ccff\n");
    printf("gpu.frames=%d\n", FRAMES);
    printf("gpu.frame_ms=%.3f\n", frame_ms);
    int ok = px[0] == 0x33 && px[1] == 0x99 && px[2] == 0xcc && px[3] == 0xff;
    int is_virgl = strstr((const char *)glGetString(GL_RENDERER), "virgl") != NULL;
    printf("gpu.virgl=%s\n", is_virgl ? "yes" : "no");
    if (glGetError() != GL_NO_ERROR)
        ok = 0;

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    gbm_device_destroy(gbm);
    close(fd);
    return ok && is_virgl ? 0 : 1;
}
