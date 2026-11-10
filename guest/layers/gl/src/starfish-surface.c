// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libstarfishsurface.so.0 on Mesa: LG's shared GPU surfaces for QtWebKit, backed by dmabufs.
 *
 * QtWebKit's GraphicsSurfaceStarfish (libQt5WebKit.so.5, WebCore/platform/graphics/surfaces/qt)
 * is how a web page's pixels get from QtWebProcess to WebAppMgr. The web process creates its
 * tile surfaces here, wraps each in an EGLImage and renders into it through an FBO
 * (QOpenGLPaintDevice); it exports them and sends the handles to the UI process, which imports
 * them, wraps them in EGLImages again and draws them into its scene. Stock firmware does all of
 * this in the PVR driver: the stock libstarfishsurface is an 8-function wrapper around the private
 * EGL entry points eglCreate/Export/Import/DestroyStarfishSurfaceLG,
 * eglAcquire/ReleaseCPUMappingStarfishSurfaceLG and eglCreateImageKHR(target 0x31E0). Mesa has
 * none of them.
 *
 * This replacement keeps the library's ABI (reverse-engineered from the stock
 * libstarfishsurface.so.0.0.0 and its only user, libQt5WebKit):
 *   struct starfish_surface *starfish_surface_create(int w, int h, unsigned usage, int format);
 *   int  starfish_surface_destroy(s);                           1 = ok
 *   int  starfish_surface_width/height/stride/format(s);
 *   void *starfish_surface_map(s);  void starfish_surface_unmap(s);   CPU access (lock/unlock)
 *   EGLImageKHR starfish_surface_create_image(s);               on eglGetCurrentDisplay()
 *   int  starfish_surface_export(s, int *id, void **blob);      returns the blob size (32) or 0;
 *                                                               the blob is malloc'd, caller frees
 *   struct starfish_surface *starfish_surface_import(int id, const void *blob);
 * The struct is 48 bytes in the stock library: +4 id (-1 = none), +8 flags, +12 width,
 * +16 height, +20 stride, +28 format, +40 CPU mapping. WebKit sends (id, 32-byte blob = bytes
 * 8..39 of the struct) to the UI process in its GraphicsSurfaceToken, and its IPC encodes the id
 * as a CoreIPC::Attachment: the id is a file descriptor, passed with SCM_RIGHTS, and the UI
 * process imports the fd it received. The IPC layer closes the sender's copy once the message is
 * sent, so every export hands out a new one. Nothing in WebKit closes the received fd; the
 * imported surface owns it.
 *
 * Buffers: a gbm_bo (ARGB8888, rendering) on the DRM render node; with virgl that is a host
 * texture. The id is a dup of its dmabuf fd, and both sides wrap the fd in an EGLImage with
 * EGL_EXT_image_dma_buf_import. CPU mappings go through gbm_bo_map/unmap, which on virgl are
 * transfers to and from the host.
 *
 * STARFISH_SURFACE_DRM overrides the render node. Errors go to syslog (PmLog). With
 * STARFISH_SURFACE_DEBUG=1, or /etc/default/starfish-surface-debug present, every call is logged to
 * /tmp/starfish-surface.log. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))

#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT         0x3270
#define EGL_LINUX_DRM_FOURCC_EXT      0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT     0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT  0x3274
#endif

/* Stock layout for the first 48 bytes; our own state follows. */
struct starfish_surface {
    void *egl;  /* +0  (stock: the driver's handle) */
    int32_t id; /* +4  last exported id (an fd, owned by the IPC message), -1 = none */
    /* +8..+39 is the exported blob */
    uint32_t flags;   /* +8  usage on create, 1 on import */
    int32_t width;    /* +12 */
    int32_t height;   /* +16 */
    int32_t stride;   /* +20 */
    int32_t unused24; /* +24 */
    int32_t format;   /* +28 LG format (4 from WebKit) */
    int32_t unused32; /* +32 */
    uint32_t fourcc;  /* +36 */
    void *map;        /* +40 CPU mapping */
    uint32_t pad;     /* +44 */
    /* private */
    struct gbm_bo *bo;
    int fd; /* dmabuf fd, owned */
    void *map_data;
    int imported;
};

_Static_assert(sizeof(void *) != 4 || __builtin_offsetof(struct starfish_surface, map) == 40,
               "stock layout");

struct blob {
    uint32_t flags;
    int32_t width, height, stride, unused24, format, unused32;
    uint32_t fourcc;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct gbm_device *gbm;
static int debug;

/* Errors go to syslog, so that they land in PmLog (/var/log/messages) with the process name.
 * The debug trace goes to a file of its own. */
#define ERR(...) syslog(LOG_USER | LOG_ERR, "starfish-surface: " __VA_ARGS__)
#define LOG(...)                                                                                   \
    do {                                                                                           \
        if (debug)                                                                                 \
            dbg(__VA_ARGS__);                                                                      \
    } while (0)

static void dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void dbg(const char *fmt, ...)
{
    char buf[512];
    struct timespec ts;
    va_list ap;
    int n, fd;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    n = snprintf(buf, sizeof(buf), "%ld.%06ld %d ", (long)ts.tv_sec, ts.tv_nsec / 1000, getpid());
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf))
        n = sizeof(buf);
    fd = open("/tmp/starfish-surface.log", O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0666);
    if (fd >= 0) {
        fchmod(fd, 0666);
        if (write(fd, buf, n) < 0) { /* nothing to do */
        }
        close(fd);
    }
}

__attribute__((constructor)) static void init(void)
{
    const char *d = getenv("STARFISH_SURFACE_DEBUG");
    debug = (d && *d && *d != '0') || access("/etc/default/starfish-surface-debug", F_OK) == 0;
}

static struct gbm_device *get_gbm(void)
{
    pthread_mutex_lock(&lock);
    if (!gbm) {
        const char *path = getenv("STARFISH_SURFACE_DRM");
        int fd;

        if (!path || !*path)
            path = "/dev/dri/renderD128";
        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            ERR("open %s: %s\n", path, strerror(errno));
        else if (!(gbm = gbm_create_device(fd))) {
            ERR("gbm_create_device failed\n");
            close(fd);
        }
    }
    pthread_mutex_unlock(&lock);
    return gbm;
}

EXPORT struct starfish_surface *starfish_surface_create(int width, int height, unsigned usage,
                                                        int format)
{
    struct gbm_device *dev = get_gbm();
    struct starfish_surface *s;

    if (!dev || width <= 0 || height <= 0)
        return NULL;
    s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->bo = gbm_bo_create(dev, width, height, GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING);
    if (!s->bo) {
        ERR("gbm_bo_create %dx%d failed\n", width, height);
        free(s);
        return NULL;
    }
    s->fd = gbm_bo_get_fd(s->bo);
    if (s->fd < 0) {
        ERR("gbm_bo_get_fd failed\n");
        gbm_bo_destroy(s->bo);
        free(s);
        return NULL;
    }
    s->id = -1;
    s->flags = usage;
    s->width = width;
    s->height = height;
    s->stride = gbm_bo_get_stride(s->bo);
    s->format = format;
    s->fourcc = GBM_FORMAT_ARGB8888;
    LOG("create %p %dx%d stride %d usage %#x format %d fd %d\n", (void *)s, width, height,
        s->stride, usage, format, s->fd);
    return s;
}

EXPORT int starfish_surface_destroy(struct starfish_surface *s)
{
    if (!s)
        return 0;
    LOG("destroy %p fd %d%s\n", (void *)s, s->fd, s->imported ? " (imported)" : "");
    if (s->map && s->bo)
        gbm_bo_unmap(s->bo, s->map_data);
    if (s->bo)
        gbm_bo_destroy(s->bo);
    if (s->fd >= 0)
        close(s->fd);
    free(s);
    return 1;
}

EXPORT int starfish_surface_width(struct starfish_surface *s)
{
    return s->width;
}
EXPORT int starfish_surface_height(struct starfish_surface *s)
{
    return s->height;
}
EXPORT int starfish_surface_stride(struct starfish_surface *s)
{
    return s->stride;
}
EXPORT int starfish_surface_format(struct starfish_surface *s)
{
    return s->format;
}

EXPORT void *starfish_surface_map(struct starfish_surface *s)
{
    uint32_t stride = 0;
    void *p;

    if (!s)
        return NULL;
    if (s->map)
        return s->map;
    if (!s->bo) {
        struct gbm_device *dev = get_gbm();
        struct gbm_import_fd_data d = {
            .fd = s->fd,
            .width = s->width,
            .height = s->height,
            .stride = s->stride,
            .format = s->fourcc,
        };
        if (dev)
            s->bo = gbm_bo_import(dev, GBM_BO_IMPORT_FD, &d, GBM_BO_USE_RENDERING);
        if (!s->bo) {
            ERR("map: gbm_bo_import failed\n");
            return NULL;
        }
    }
    p = gbm_bo_map(s->bo, 0, 0, s->width, s->height, GBM_BO_TRANSFER_READ_WRITE, &stride,
                   &s->map_data);
    if (!p) {
        ERR("map: gbm_bo_map failed\n");
        return NULL;
    }
    if ((int)stride != s->stride) {
        /* WebKit asked for the stride before mapping; it has to stay the same. */
        ERR("map: stride %u, expected %d\n", stride, s->stride);
        gbm_bo_unmap(s->bo, s->map_data);
        s->map_data = NULL;
        return NULL;
    }
    LOG("map %p -> %p\n", (void *)s, p);
    s->map = p;
    return p;
}

EXPORT void starfish_surface_unmap(struct starfish_surface *s)
{
    if (!s || !s->map)
        return;
    LOG("unmap %p\n", (void *)s);
    gbm_bo_unmap(s->bo, s->map_data);
    s->map = NULL;
    s->map_data = NULL;
}

EXPORT EGLImageKHR starfish_surface_create_image(struct starfish_surface *s)
{
    static PFNEGLCREATEIMAGEKHRPROC create_image;
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLImageKHR img;
    EGLint attrs[] = {
        EGL_WIDTH,
        s->width,
        EGL_HEIGHT,
        s->height,
        EGL_LINUX_DRM_FOURCC_EXT,
        (EGLint)s->fourcc,
        EGL_DMA_BUF_PLANE0_FD_EXT,
        s->fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT,
        0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,
        s->stride,
        EGL_IMAGE_PRESERVED_KHR,
        EGL_TRUE,
        EGL_NONE,
    };

    if (!create_image)
        create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    if (!create_image || dpy == EGL_NO_DISPLAY || s->fd < 0) {
        ERR("create_image: no display (%p) or fd (%d)\n", dpy, s->fd);
        return EGL_NO_IMAGE_KHR;
    }
    img = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
    if (img == EGL_NO_IMAGE_KHR)
        ERR("create_image: eglCreateImageKHR(dmabuf %dx%d) failed, %#x\n", s->width, s->height,
            eglGetError());
    LOG("create_image %p fd %d -> %p\n", (void *)s, s->fd, img);
    return img;
}

EXPORT int starfish_surface_export(struct starfish_surface *s, int32_t *id, void **out)
{
    struct blob *b;
    int fd;

    if (!s || !out)
        return 0;
    /* a new fd each time: the IPC message that carries it closes it after sending */
    fd = fcntl(s->fd, F_DUPFD_CLOEXEC, 0);
    if (fd < 0) {
        ERR("export: dup: %s\n", strerror(errno));
        return 0;
    }
    b = malloc(sizeof(*b));
    *out = b;
    if (!b) {
        close(fd);
        return 0;
    }
    s->id = fd;
    if (id)
        *id = fd;
    memcpy(b, &s->flags, sizeof(*b));
    LOG("export %p fd %d -> id %d\n", (void *)s, s->fd, fd);
    return sizeof(*b);
}

EXPORT struct starfish_surface *starfish_surface_import(int32_t id, const void *blob)
{
    struct starfish_surface *s;

    if (id < 0 || !blob) {
        LOG("import id %d blob %p: nothing to import\n", id, blob);
        return NULL;
    }
    s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    memcpy(&s->flags, blob, sizeof(struct blob));
    s->id = id;
    s->flags = 1;
    s->imported = 1;
    s->fd = id; /* received through SCM_RIGHTS; ours now */
    LOG("import id %d -> %p %dx%d stride %d\n", id, (void *)s, s->width, s->height, s->stride);
    return s;
}
