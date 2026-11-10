// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Cross-process test of the gl layer's libstarfishsurface, the way QtWebKit uses it.
 *
 * The child plays QtWebProcess: it creates a surface, wraps it in an EGLImage, renders into it
 * through an FBO (a clear, then a triangle), flushes with an EGL fence as GraphicsSurfaceStarfish
 * does, exports it and sends (id, blob) to the parent, the id with SCM_RIGHTS as WebKit's IPC does.
 * The parent plays WebAppMgr: it imports the surface, wraps it in an EGLImage and reads the
 * pixels back through an FBO. It also maps the surface and reads the same pixels from the CPU.
 *
 *   starfish-surface-test        (in the guest, inside webos-enter; needs /dev/dri/renderD128)
 * Prints the pixels and PASS/FAIL. EGL runs on Mesa's surfaceless platform. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

struct starfish_surface;
struct starfish_surface *starfish_surface_create(int w, int h, unsigned usage, int format);
int starfish_surface_destroy(struct starfish_surface *s);
int starfish_surface_stride(struct starfish_surface *s);
void *starfish_surface_map(struct starfish_surface *s);
void starfish_surface_unmap(struct starfish_surface *s);
EGLImageKHR starfish_surface_create_image(struct starfish_surface *s);
int starfish_surface_export(struct starfish_surface *s, int32_t *id, void **blob);
struct starfish_surface *starfish_surface_import(int32_t id, const void *blob);

#define W 256
#define H 128

static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target;

static void egl_up(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    static const EGLint cattr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    static const EGLint conf_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE,
                                       EGL_PBUFFER_BIT, EGL_NONE};
    EGLConfig conf;
    EGLint n;
    EGLContext ctx;

    if (!eglInitialize(dpy, NULL, NULL) || !eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(dpy, conf_attr, &conf, 1, &n) || n < 1 ||
        !(ctx = eglCreateContext(dpy, conf, EGL_NO_CONTEXT, cattr)) ||
        !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        fprintf(stderr, "[%d] EGL setup failed: %#x\n", getpid(), eglGetError());
        exit(2);
    }
    image_target =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
}

static GLuint fbo_for(struct starfish_surface *s)
{
    EGLImageKHR img = starfish_surface_create_image(s);
    GLuint tex, fbo;
    GLenum st;

    if (img == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "[%d] create_image failed\n", getpid());
        exit(3);
    }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    image_target(GL_TEXTURE_2D, img);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("[%d] FBO status %#x, GL error %#x\n", getpid(), st, glGetError());
    return fbo;
}

static void draw_triangle(void)
{
    static const char *vs = "attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }";
    static const char *fs =
        "precision mediump float; void main() { gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0); }";
    static const GLfloat v[] = {-1, -1, 1, -1, -1, 1}; /* lower-left half */
    GLuint p = glCreateProgram(), a = glCreateShader(GL_VERTEX_SHADER),
           b = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(a, 1, &vs, NULL);
    glCompileShader(a);
    glShaderSource(b, 1, &fs, NULL);
    glCompileShader(b);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glBindAttribLocation(p, 0, "p");
    glLinkProgram(p);
    glUseProgram(p);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, v);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void fence_flush(void)
{
    PFNEGLCREATESYNCKHRPROC cs = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
    PFNEGLCLIENTWAITSYNCKHRPROC cw =
        (PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR");
    PFNEGLDESTROYSYNCKHRPROC ds = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLSyncKHR s = cs(dpy, EGL_SYNC_FENCE_KHR, NULL);

    if (s == EGL_NO_SYNC_KHR) {
        printf("[%d] eglCreateSyncKHR failed %#x, glFinish instead\n", getpid(), eglGetError());
        glFinish();
        return;
    }
    printf("[%d] fence wait %#x\n", getpid(),
           cw(dpy, s, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR));
    ds(dpy, s);
}

static int check(const char *what, const uint8_t *px, int stride_px, int bgra)
{
    /* rows are GL rows (row 0 = y 0, both for glReadPixels and for the buffer's memory, which
     * is the texture's level 0): upper right is the clear colour (red), lower left the triangle */
    const uint8_t *ur = px + ((3 * H / 4) * stride_px + 3 * W / 4) * 4;
    const uint8_t *ll = px + ((H / 4) * stride_px + W / 4) * 4;
    int r = bgra ? 2 : 0, b = bgra ? 0 : 2, ok;

    printf("%s: upper-right %02x%02x%02x%02x, lower-left %02x%02x%02x%02x (as %s)\n", what, ur[0],
           ur[1], ur[2], ur[3], ll[0], ll[1], ll[2], ll[3], bgra ? "BGRA" : "RGBA");
    ok = ur[r] == 0xff && ur[b] == 0 && ll[b] == 0xff && ll[r] == 0;
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

int main(void)
{
    int sv[2], status, ok = 1;
    pid_t pid;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
        return perror("socketpair"), 2;
    pid = fork();
    if (pid == 0) { /* QtWebProcess */
        struct starfish_surface *s;
        int32_t id;
        void *blob;
        int len;
        char cbuf[CMSG_SPACE(sizeof(int))], ack;
        struct iovec iov;
        struct msghdr mh = {0};
        struct cmsghdr *cm;

        egl_up();
        s = starfish_surface_create(W, H, 0x8000000f, 4);
        if (!s)
            return fprintf(stderr, "create failed\n"), 3;
        fbo_for(s);
        glViewport(0, 0, W, H);
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        draw_triangle();
        fence_flush();
        len = starfish_surface_export(s, &id, &blob);
        printf("[%d] exported id %d, blob %d bytes\n", getpid(), id, len);
        iov.iov_base = blob;
        iov.iov_len = len;
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cbuf;
        mh.msg_controllen = sizeof(cbuf);
        cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &id, sizeof(int));
        if (sendmsg(sv[1], &mh, 0) < 0)
            return perror("sendmsg"), 3;
        close(id); /* as CoreIPC does once the message is out */
        free(blob);
        if (read(sv[1], &ack, 1) != 1) { /* keep the surface until the parent has looked */
        }
        starfish_surface_destroy(s);
        return 0;
    } else { /* WebAppMgr */
        char blob[64], cbuf[CMSG_SPACE(sizeof(int))];
        struct iovec iov = {blob, sizeof(blob)};
        struct msghdr mh = {0};
        struct cmsghdr *cm;
        struct starfish_surface *s;
        uint8_t px[W * H * 4];
        uint8_t *m;
        int fd = -1;

        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cbuf;
        mh.msg_controllen = sizeof(cbuf);
        if (recvmsg(sv[0], &mh, 0) <= 0)
            return perror("recvmsg"), 2;
        for (cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm))
            if (cm->cmsg_type == SCM_RIGHTS)
                memcpy(&fd, CMSG_DATA(cm), sizeof(int));
        printf("[%d] received fd %d\n", getpid(), fd);
        egl_up();
        s = starfish_surface_import(fd, blob);
        if (!s)
            return fprintf(stderr, "import failed\n"), 2;
        fbo_for(s);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        ok &= check("GPU readback in the importer", px, W, 0);
        m = starfish_surface_map(s);
        if (m) {
            ok &= check("CPU map in the importer", m, starfish_surface_stride(s) / 4, 1);
            starfish_surface_unmap(s);
        } else {
            printf("FAIL map\n");
            ok = 0;
        }
        if (write(sv[0], "x", 1) != 1) { /* child exits anyway */
        }
        waitpid(pid, &status, 0);
        starfish_surface_destroy(s);
        printf("%s\n", ok && WIFEXITED(status) && !WEXITSTATUS(status) ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
}
