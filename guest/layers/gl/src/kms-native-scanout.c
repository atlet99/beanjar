// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Native Qt KMS presentation on virtio-gpu's opaque primary plane.
 *
 * Keep the caller's ARGB framebuffer intact. Only when it is bound to a real
 * XR24-only primary plane, submit a second, XRGB view of the same allocation.
 * Qt retains GBM ownership, commits and kernel completion events. This library
 * neither owns EGL nor invents DRM objects, capabilities or completions.
 *
 * Opt in by preloading this library into the native KMS compositor only, with
 * BEANJAR_NATIVE_KMS=1 (the supplied WebOS KMS integration, one display) or
 * BEANJAR_NATIVE_KMS=view (the view alone: the compositor's own integration,
 * such as O22N2's stock Starfish one, and its own configuration stay). The
 * libdrm ioctl boundary covers both legacy and serialized atomic requests,
 * without depending on libdrm's private atomic-request representation.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define EXPORT __attribute__((visibility("default")))

struct framebuffer {
    struct drm_mode_fb_cmd2 original;
    uint32_t alias;
    struct framebuffer *next;
};
struct client {
    unsigned references;
    struct framebuffer *fbs;
};
struct descriptor {
    int fd;
    struct client *client;
    struct descriptor *next;
};
static struct descriptor *descriptors;
static int enabled;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t symbols_once = PTHREAD_ONCE_INIT;
static int (*next_ioctl)(int, unsigned long, void *);
static int (*next_close)(int), (*next_dup)(int), (*next_dup2)(int, int);
static int (*next_dup3)(int, int, int), (*next_fcntl)(int, int, ...);
static int (*next_fcntl64)(int, int, ...);
static int (*next_close_range)(unsigned, unsigned, int);

static void symbols(void)
{
    next_ioctl = dlsym(RTLD_NEXT, "drmIoctl");
    next_close = dlsym(RTLD_NEXT, "close");
    next_dup = dlsym(RTLD_NEXT, "dup");
    next_dup2 = dlsym(RTLD_NEXT, "dup2");
    next_dup3 = dlsym(RTLD_NEXT, "dup3");
    next_fcntl = dlsym(RTLD_NEXT, "fcntl");
    next_fcntl64 = dlsym(RTLD_NEXT, "fcntl64");
    next_close_range = dlsym(RTLD_NEXT, "close_range");
}

static struct descriptor *find_descriptor(int fd)
{
    for (struct descriptor *d = descriptors; d; d = d->next)
        if (d->fd == fd)
            return d;
    return NULL;
}

static void forget_descriptor(int fd)
{
    struct descriptor **at = &descriptors;
    while (*at && (*at)->fd != fd)
        at = &(*at)->next;
    if (!*at)
        return;
    struct descriptor *d = *at;
    *at = d->next;
    struct client *c = d->client;
    free(d);
    if (--c->references)
        return;
    /* No duplicated descriptor is held by the adapter. Linux releases all
     * original and alias FBs when the last real open-file reference closes.
     */
    while (c->fbs) {
        struct framebuffer *fb = c->fbs;
        c->fbs = fb->next;
        free(fb);
    }
    free(c);
}

/* Called for registration and before duplicating a possibly unobserved DRM
 * fd. Establish the group before dup, so even dup-before-AddFB2 is tracked.
 */
static struct descriptor *get_descriptor(int fd, int create)
{
    struct descriptor *d = find_descriptor(fd);
    if (d || !create)
        return d;
    struct stat st;
    if (fstat(fd, &st))
        return NULL;
#ifndef KMS_NATIVE_TEST
    if (!S_ISCHR(st.st_mode) || major(st.st_rdev) != 226 /* Linux DRM major */)
        return NULL;
#endif
    drmVersion *v = drmGetVersion(fd);
    if (!v)
        return NULL;
    int match = v->name_len == strlen("virtio_gpu") && !memcmp(v->name, "virtio_gpu", v->name_len);
    drmFreeVersion(v);
    if (!match)
        return NULL;
    d = calloc(1, sizeof *d);
    struct client *c = calloc(1, sizeof *c);
    if (!d || !c) {
        free(d);
        free(c);
        return NULL;
    }
    c->references = 1;
    d->fd = fd;
    d->client = c;
    d->next = descriptors;
    descriptors = d;
    return d;
}

/* close and the first-alias diagnostic can be cancellation points. Keep the
 * whole serialized operation indivisible, then restore the caller's state only
 * after unlocking. A pending cancellation can never leave this mutex held.
 */
static int enter_locked(void)
{
    int state;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);
    pthread_mutex_lock(&lock);
    return state;
}

static void leave_locked(int state, int saved_errno)
{
    pthread_mutex_unlock(&lock);
    errno = saved_errno;
    pthread_setcancelstate(state, NULL);
    errno = saved_errno;
}

static __thread int fork_cancel_state;
static void fork_prepare(void)
{
    fork_cancel_state = enter_locked();
}
static void fork_done(void)
{
    leave_locked(fork_cancel_state, errno);
}

static void drop_self_preload(void)
{
    const char *value = getenv("LD_PRELOAD");
    if (!value)
        return;
    char *input = strdup(value), *output = calloc(strlen(value) + 1, 1);
    if (!input || !output) {
        free(input);
        free(output);
        return;
    }
    char *save = NULL;
    for (char *word = strtok_r(input, " :\t\n", &save); word;
         word = strtok_r(NULL, " :\t\n", &save)) {
        const char *base = strrchr(word, '/');
        if (!strcmp(base ? base + 1 : word, "libkms-native-scanout.so"))
            continue;
        if (*output)
            strcat(output, " ");
        strcat(output, word);
    }
    if (*output)
        setenv("LD_PRELOAD", output, 1);
    else
        unsetenv("LD_PRELOAD");
    free(input);
    free(output);
}

__attribute__((constructor)) static void initialize(void)
{
    pthread_once(&symbols_once, symbols);
    const char *flag = getenv("BEANJAR_NATIVE_KMS");
    char path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (!flag || (strcmp(flag, "1") && strcmp(flag, "view")) || n < 0)
        return;
    path[n] = 0;
    const char *base = strrchr(path, '/');
#ifndef KMS_NATIVE_TEST
    if (strcmp(base ? base + 1 : path, "surface-manager"))
        return;
#else
    (void)base;
#endif
    enabled = 1;
    pthread_atfork(fork_prepare, fork_done, fork_done);
    if (!strcmp(flag, "view")) {
        drop_self_preload();
        return;
    }
    setenv("QT_QPA_EGLFS_INTEGRATION", "eglfs_kms_webos", 1);
    setenv("WEBOS_COMPOSITOR_DISPLAYS", "1", 1);
    setenv("WEBOS_COMPOSITOR_EXTRA_WINDOWS", "[]", 1);
    /* The stock EventFilter skips mouse-driven cursor visibility whenever
     * this variable is present. Native KMS receives genuine Qt mouse events;
     * select its stock unset-variable policy instead of Starfish IM keys.
     */
    unsetenv("WEBOS_CURSOR_VISIBLE_EVENT");
    drop_self_preload();
}

static struct framebuffer *find_fb(struct client *c, uint32_t id)
{
    for (struct framebuffer *fb = c ? c->fbs : NULL; fb; fb = fb->next)
        if (fb->original.fb_id == id)
            return fb;
    return NULL;
}

/* A successful single-plane ARGB registration, implicit or with the linear
 * modifier (the same bytes: virtio-gpu's planes list LINEAR with
 * virtio_gpu.modifiers=1), is the only source for an alias. Any other
 * modifier stays the driver's contract. Preserve all original metadata,
 * including offset, flags and modifier.
 */
static int eligible(const struct drm_mode_fb_cmd2 *f)
{
    return f->pixel_format == DRM_FORMAT_ARGB8888 &&
           (!(f->flags & DRM_MODE_FB_MODIFIERS) || f->modifier[0] == DRM_FORMAT_MOD_LINEAR) &&
           f->handles[0] && !f->handles[1] && !f->handles[2] && !f->handles[3];
}

/* Returns the actual primary's FB_ID property when requested. */
static int xrgb_primary(int fd, uint32_t id, uint32_t crtc_mask, uint32_t *fb_prop)
{
    drmModePlane *plane = drmModeGetPlane(fd, id);
    if (!plane)
        return 0;
    int xr = 0, ar = 0;
    for (uint32_t i = 0; i < plane->count_formats; i++) {
        xr |= plane->formats[i] == DRM_FORMAT_XRGB8888;
        ar |= plane->formats[i] == DRM_FORMAT_ARGB8888;
    }
    int compatible = !crtc_mask || (plane->possible_crtcs & crtc_mask);
    drmModeFreePlane(plane);
    if (!xr || ar || !compatible)
        return 0;
    drmModeObjectProperties *props = drmModeObjectGetProperties(fd, id, DRM_MODE_OBJECT_PLANE);
    if (!props)
        return 0;
    int primary = 0;
    uint32_t property = 0;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);
        if (!p)
            continue;
        if (!strcmp(p->name, "type"))
            primary = props->prop_values[i] == DRM_PLANE_TYPE_PRIMARY;
        if (!strcmp(p->name, "FB_ID"))
            property = p->prop_id;
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    if (fb_prop)
        *fb_prop = property;
    return primary;
}

static int crtc_has_xrgb_primary(int fd, uint32_t crtc)
{
    drmModeRes *res = drmModeGetResources(fd);
    if (!res)
        return 0;
    uint32_t mask = 0;
    for (int i = 0; i < res->count_crtcs && i < 32; i++)
        if (res->crtcs[i] == crtc)
            mask = UINT32_C(1) << i;
    drmModeFreeResources(res);
    if (!mask)
        return 0;
    drmModePlaneRes *planes = drmModeGetPlaneResources(fd);
    if (!planes)
        return 0;
    int match = 0;
    for (uint32_t i = 0; i < planes->count_planes && !match; i++)
        match = xrgb_primary(fd, planes->planes[i], mask, NULL);
    drmModeFreePlaneResources(planes);
    return match;
}

static int alias_fb(int fd, struct framebuffer *fb, uint32_t *id)
{
    if (!fb->alias) {
        struct drm_mode_fb_cmd2 alias = fb->original;
        alias.fb_id = 0;
        alias.pixel_format = DRM_FORMAT_XRGB8888;
        if (next_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &alias))
            return -1;
        fb->alias = alias.fb_id;
        fprintf(stderr, "[kms-native] primary view AR24 fb %u -> XR24 fb %u\n", fb->original.fb_id,
                fb->alias);
    }
    *id = fb->alias;
    return 0;
}

static int atomic_commit(int fd, struct client *c, const struct drm_mode_atomic *input)
{
    const uint32_t *objects = (const void *)(uintptr_t)input->objs_ptr;
    const uint32_t *counts = (const void *)(uintptr_t)input->count_props_ptr;
    const uint32_t *props = (const void *)(uintptr_t)input->props_ptr;
    const uint64_t *values = (const void *)(uintptr_t)input->prop_values_ptr;
    size_t total = 0;
    if (!input->count_objs || !objects || !counts || !props || !values)
        return next_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, (void *)input);
    for (uint32_t i = 0; i < input->count_objs; i++) {
        if (counts[i] > SIZE_MAX / sizeof(*values) - total) {
            errno = EOVERFLOW;
            return -1;
        }
        total += counts[i];
    }
    uint64_t *copy = NULL;
    size_t at = 0;
    for (uint32_t i = 0; i < input->count_objs; i++) {
        /* Check whether this object even references one of our source FBs
         * before issuing plane queries (CRTC/connector ids are not planes).
         */
        int candidate = 0;
        for (uint32_t j = 0; j < counts[i]; j++)
            if (values[at + j] <= UINT32_MAX && find_fb(c, (uint32_t)values[at + j]))
                candidate = 1;
        uint32_t property = 0;
        if (candidate && xrgb_primary(fd, objects[i], 0, &property) && property) {
            for (uint32_t j = 0; j < counts[i]; j++) {
                if (props[at + j] != property || values[at + j] > UINT32_MAX)
                    continue;
                struct framebuffer *fb = find_fb(c, (uint32_t)values[at + j]);
                if (!fb)
                    continue;
                if (!copy) {
                    copy = malloc(total * sizeof(*copy));
                    if (!copy) {
                        errno = ENOMEM;
                        return -1;
                    }
                    memcpy(copy, values, total * sizeof(*copy));
                }
                uint32_t id;
                if (alias_fb(fd, fb, &id)) {
                    int saved = errno;
                    free(copy);
                    errno = saved;
                    return -1;
                }
                copy[at + j] = id;
            }
        }
        at += counts[i];
    }
    struct drm_mode_atomic output = *input;
    if (copy)
        output.prop_values_ptr = (uintptr_t)copy;
    int result = next_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &output);
    int saved = errno;
    free(copy);
    errno = saved;
    return result;
}

static int remove_fb(int fd, unsigned long request, void *arg, struct client *c)
{
    uint32_t id =
        request == DRM_IOCTL_MODE_RMFB ? *(uint32_t *)arg : ((struct drm_mode_closefb *)arg)->fb_id;
    struct framebuffer **at = c ? &c->fbs : NULL;
    while (at && *at && (*at)->original.fb_id != id)
        at = &(*at)->next;
    struct framebuffer *fb = at ? *at : NULL;
    if (fb && fb->alias) {
        /* CloseFB preserves a displayed buffer; RmFB removes it. Use the
         * caller's actual operation for both views. On failure retain state
         * and original ownership, allowing a genuine retry.
         */
        struct drm_mode_closefb close = {.fb_id = fb->alias};
        void *alias_arg = request == DRM_IOCTL_MODE_RMFB ? (void *)&fb->alias : (void *)&close;
        if (next_ioctl(fd, request, alias_arg))
            return -1;
        fb->alias = 0;
    }
    int result = next_ioctl(fd, request, arg);
    if (!result && fb) {
        *at = fb->next;
        free(fb);
    }
    return result;
}

EXPORT int drmIoctl(int fd, unsigned long request, void *arg)
{
    pthread_once(&symbols_once, symbols);
    if (!next_ioctl) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled)
        return next_ioctl(fd, request, arg);
    switch (request) {
    case DRM_IOCTL_MODE_ADDFB2:
    case DRM_IOCTL_MODE_RMFB:
    case DRM_IOCTL_MODE_CLOSEFB:
    case DRM_IOCTL_MODE_SETCRTC:
    case DRM_IOCTL_MODE_PAGE_FLIP:
    case DRM_IOCTL_MODE_SETPLANE:
    case DRM_IOCTL_MODE_ATOMIC:
        break;
    default:
        return next_ioctl(fd, request, arg);
    }
    int incoming_errno = errno;
    if (!arg)
        return next_ioctl(fd, request, arg);
    int cancel_state = enter_locked();
    struct descriptor *d = get_descriptor(fd, request == DRM_IOCTL_MODE_ADDFB2);
    struct client *c = d ? d->client : NULL;
    int result;
    if (request == DRM_IOCTL_MODE_ADDFB2) {
        struct drm_mode_fb_cmd2 *f = arg;
        struct framebuffer *fb = c && eligible(f) ? calloc(1, sizeof *fb) : NULL;
        /* If tracking cannot be allocated, preserve the real registration;
         * later bindings follow the unadapted kernel path.
         */
        errno = incoming_errno;
        result = next_ioctl(fd, request, arg);
        int saved = errno;
        if (!result && fb) {
            fb->original = *f;
            fb->next = c->fbs;
            c->fbs = fb;
        } else
            free(fb);
        errno = saved;
    } else if (!c) {
        errno = incoming_errno;
        result = next_ioctl(fd, request, arg);
    } else if (request == DRM_IOCTL_MODE_RMFB || request == DRM_IOCTL_MODE_CLOSEFB) {
        result = remove_fb(fd, request, arg, c);
    } else if (request == DRM_IOCTL_MODE_ATOMIC) {
        result = atomic_commit(fd, c, arg);
    } else {
        /* Local copies preserve the caller's original framebuffer metadata. */
        struct drm_mode_crtc crtc;
        struct drm_mode_crtc_page_flip_target flip;
        struct drm_mode_set_plane plane;
        uint32_t id, target;
        void *output;
        if (request == DRM_IOCTL_MODE_SETCRTC) {
            crtc = *(struct drm_mode_crtc *)arg;
            id = crtc.fb_id;
            target = crtc.crtc_id;
            output = &crtc;
        } else if (request == DRM_IOCTL_MODE_PAGE_FLIP) {
            /* PAGE_FLIP and PAGE_FLIP_TARGET share the ioctl and ABI size. */
            memcpy(&flip, arg, sizeof flip);
            id = flip.fb_id;
            target = flip.crtc_id;
            output = &flip;
        } else {
            plane = *(struct drm_mode_set_plane *)arg;
            id = plane.fb_id;
            target = plane.plane_id;
            output = &plane;
        }
        struct framebuffer *fb = find_fb(c, id);
        int primary =
            fb && (request == DRM_IOCTL_MODE_SETPLANE ? xrgb_primary(fd, target, 0, NULL)
                                                      : crtc_has_xrgb_primary(fd, target));
        if (primary && alias_fb(fd, fb, &id))
            result = -1;
        else {
            if (request == DRM_IOCTL_MODE_SETCRTC)
                crtc.fb_id = id;
            else if (request == DRM_IOCTL_MODE_PAGE_FLIP)
                flip.fb_id = id;
            else
                plane.fb_id = id;
            errno = incoming_errno;
            result = next_ioctl(fd, request, output);
        }
    }
    int saved = errno;
    leave_locked(cancel_state, saved);
    return result;
}

/* Descriptor bookkeeping follows libc's real close/dup operations. No private
 * duplicate pins DRM master after the compositor closes it. Raw close syscalls
 * bypass this library boundary, just as raw DRM ioctls bypass its translation.
 */
EXPORT int close(int fd)
{
    pthread_once(&symbols_once, symbols);
    if (!next_close) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled)
        return next_close(fd);
    int cancel_state = enter_locked();
    int result = next_close(fd), saved = errno;
    /* Linux releases the fd even when close reports EINTR or a flush error. */
    forget_descriptor(fd);
    leave_locked(cancel_state, saved);
    return result;
}

static int duplicate(int fd, int target, int flags, int operation)
{
    pthread_once(&symbols_once, symbols);
    int cancel_state = enter_locked();
    struct descriptor *source = enabled ? get_descriptor(fd, 1) : NULL;
    struct descriptor *copy = source ? malloc(sizeof *copy) : NULL;
    if (source && !copy) {
        leave_locked(cancel_state, ENOMEM);
        return -1;
    }
    int result;
    if (operation == -1)
        result = next_dup ? next_dup(fd) : (errno = ENOSYS, -1);
    else if (operation == -2)
        result = next_dup2 ? next_dup2(fd, target) : (errno = ENOSYS, -1);
    else if (operation == -3)
        result = next_dup3 ? next_dup3(fd, target, flags) : (errno = ENOSYS, -1);
    else
        result = next_fcntl ? next_fcntl(fd, operation, target) : (errno = ENOSYS, -1);
    int saved = errno;
    if (result >= 0 && result != fd) {
        /* Hold the source before dup2 replaces another fd of the same group. */
        if (source)
            source->client->references++;
        struct client *c = source ? source->client : NULL;
        forget_descriptor(result);
        if (copy) {
            *copy = (struct descriptor){result, c, descriptors};
            descriptors = copy;
            copy = NULL;
        }
    }
    free(copy);
    leave_locked(cancel_state, saved);
    return result;
}
EXPORT int dup(int fd)
{
    return duplicate(fd, -1, 0, -1);
}
EXPORT int dup2(int fd, int target)
{
    return duplicate(fd, target, 0, -2);
}
EXPORT int dup3(int fd, int target, int flags)
{
    return duplicate(fd, target, flags, -3);
}

static int fcntl_call(int fd, int cmd, va_list args, int large)
{
    pthread_once(&symbols_once, symbols);
    int (*real)(int, int, ...) = large ? next_fcntl64 : next_fcntl;
    if (!real) {
        errno = ENOSYS;
        return -1;
    }
    switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
        return duplicate(fd, va_arg(args, int), 0, cmd);
    case F_GETFD:
    case F_GETFL:
    case F_GETOWN:
#ifdef F_GETSIG
    case F_GETSIG:
#endif
#ifdef F_GETLEASE
    case F_GETLEASE:
#endif
#ifdef F_GETPIPE_SZ
    case F_GETPIPE_SZ:
#endif
#ifdef F_GET_SEALS
    case F_GET_SEALS:
#endif
        return real(fd, cmd);
    case F_SETFD:
    case F_SETFL:
    case F_SETOWN:
#ifdef F_SETSIG
    case F_SETSIG:
#endif
#ifdef F_SETLEASE
    case F_SETLEASE:
#endif
#ifdef F_NOTIFY
    case F_NOTIFY:
#endif
#ifdef F_SETPIPE_SZ
    case F_SETPIPE_SZ:
#endif
#ifdef F_ADD_SEALS
    case F_ADD_SEALS:
#endif
        return real(fd, cmd, va_arg(args, int));
    default:
        return real(fd, cmd, va_arg(args, void *));
    }
}
EXPORT int fcntl(int fd, int cmd, ...)
{
    va_list args;
    va_start(args, cmd);
    int result = fcntl_call(fd, cmd, args, 0);
    va_end(args);
    return result;
}
EXPORT int fcntl64(int fd, int cmd, ...)
{
    va_list args;
    va_start(args, cmd);
    int result = fcntl_call(fd, cmd, args, 1);
    va_end(args);
    return result;
}

EXPORT int close_range(unsigned first, unsigned last, int flags)
{
    pthread_once(&symbols_once, symbols);
    if (!next_close_range) {
        errno = ENOSYS;
        return -1;
    }
    if (!enabled)
        return next_close_range(first, last, flags);
    /* CLOSE_RANGE_UNSHARE (bit 1) creates a thread-private fd namespace.
     * This process-scoped adapter cannot track that relationship honestly;
     * reject it before the kernel changes any descriptor table. Ordinary
     * close-range and CLOEXEC remain supported.
     */
    if (flags & (1 << 1)) {
        errno = EOPNOTSUPP;
        return -1;
    }
    int cancel_state = enter_locked();
    int result = next_close_range(first, last, flags), saved = errno;
    /* Linux CLOSE_RANGE_CLOEXEC is bit 2; it does not close the descriptors. */
    if (!result && !(flags & (1 << 2))) {
        struct descriptor *d = descriptors;
        while (d) {
            int fd = d->fd;
            d = d->next;
            if ((unsigned)fd >= first && (unsigned)fd <= last)
                forget_descriptor(fd);
        }
    }
    leave_locked(cancel_state, saved);
    return result;
}

__attribute__((destructor)) static void finish(void)
{
    /* Live objects still belong to the caller and are released by Linux. */
    while (descriptors)
        forget_descriptor(descriptors->fd);
}
