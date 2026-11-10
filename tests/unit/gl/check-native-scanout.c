// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Exercise the owned translation boundary, including kernel-style rejection.
 * Real presentation is separately validated on the primary VM target.
 * Plane formats/types follow virtgpu_plane.c; framebuffer/layout and ioctl
 * expectations follow Linux DRM UAPI, not a synthetic success-only backend.
 */
#define KMS_NATIVE_TEST
#define close       adapter_close
#define dup         adapter_dup
#define dup2        adapter_dup2
#define dup3        adapter_dup3
#define fcntl       adapter_fcntl
#define fcntl64     adapter_fcntl64
#define close_range adapter_close_range
#define drmIoctl    adapter_ioctl
#include "../../../guest/layers/gl/src/kms-native-scanout.c"
#undef close
#undef dup
#undef dup2
#undef dup3
#undef fcntl
#undef fcntl64
#undef close_range
#undef drmIoctl
#include <assert.h>

static struct drm_mode_fb_cmd2 fbs[4096];
static unsigned next_id = 100, adds, removals, flips, commits;
static uint32_t displayed, fail_remove;
static int fail_add, fail_present, primary_argb;
static unsigned last_flags;
static uint64_t last_data;
static uint32_t last_plane;

/* Topology mirrors the measured virtio primary/cursor distinction; numeric
 * ids are opaque test objects. A second CRTC checks possible_crtcs handling.
 */
drmModeRes *drmModeGetResources(int fd)
{
    (void)fd;
    drmModeRes *r = calloc(1, sizeof *r);
    r->count_crtcs = 2;
    r->crtcs = malloc(2 * sizeof *r->crtcs);
    r->crtcs[0] = 50;
    r->crtcs[1] = 51;
    return r;
}
drmModePlaneRes *drmModeGetPlaneResources(int fd)
{
    (void)fd;
    drmModePlaneRes *r = calloc(1, sizeof *r);
    r->count_planes = 2;
    r->planes = malloc(2 * sizeof *r->planes);
    r->planes[0] = 60;
    r->planes[1] = 61;
    return r;
}
drmModePlane *drmModeGetPlane(int fd, uint32_t id)
{
    (void)fd;
    if (id != 60 && id != 61) {
        errno = ENOENT;
        return NULL;
    }
    drmModePlane *r = calloc(1, sizeof *r);
    r->plane_id = id;
    r->possible_crtcs = 1;
    r->count_formats = id == 60 && primary_argb ? 2 : 1;
    r->formats = malloc(r->count_formats * sizeof *r->formats);
    r->formats[0] = id == 60 ? DRM_FORMAT_XRGB8888 : DRM_FORMAT_ARGB8888;
    if (r->count_formats == 2)
        r->formats[1] = DRM_FORMAT_ARGB8888;
    return r;
}
drmModeObjectProperties *drmModeObjectGetProperties(int fd, uint32_t id, uint32_t type)
{
    (void)fd;
    assert(type == DRM_MODE_OBJECT_PLANE && (id == 60 || id == 61));
    drmModeObjectProperties *r = calloc(1, sizeof *r);
    r->count_props = 2;
    r->props = malloc(2 * sizeof *r->props);
    r->prop_values = calloc(2, sizeof *r->prop_values);
    r->props[0] = 70;
    r->props[1] = 71;
    r->prop_values[0] = id == 60 ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_CURSOR;
    return r;
}
drmModePropertyRes *drmModeGetProperty(int fd, uint32_t id)
{
    (void)fd;
    drmModePropertyRes *r = calloc(1, sizeof *r);
    r->prop_id = id;
    strcpy(r->name, id == 70 ? "type" : "FB_ID");
    return r;
}
drmVersion *drmGetVersion(int fd)
{
    (void)fd;
    drmVersion *v = calloc(1, sizeof *v);
    v->name = strdup("virtio_gpu");
    v->name_len = strlen(v->name);
    return v;
}
static int present(uint32_t id, int primary)
{
    if (fail_present) {
        errno = fail_present;
        return -1;
    }
    if (id && (!fbs[id].fb_id ||
               (primary && !primary_argb && fbs[id].pixel_format != DRM_FORMAT_XRGB8888))) {
        errno = EINVAL;
        return -1;
    }
    displayed = id;
    return 0;
}
static int kernel_ioctl(int fd, unsigned long req, void *arg)
{
    (void)fd;
    if (req == DRM_IOCTL_MODE_ADDFB2) {
        struct drm_mode_fb_cmd2 *f = arg;
        if (fail_add) {
            errno = fail_add;
            return -1;
        }
        assert(next_id < 4096);
        f->fb_id = next_id++;
        fbs[f->fb_id] = *f;
        adds++;
        return 0;
    }
    if (req == DRM_IOCTL_MODE_GETFB2) {
        struct drm_mode_fb_cmd2 *f = arg;
        if (!fbs[f->fb_id].fb_id) {
            errno = ENOENT;
            return -1;
        }
        *f = fbs[f->fb_id];
        return 0;
    }
    if (req == DRM_IOCTL_MODE_RMFB || req == DRM_IOCTL_MODE_CLOSEFB) {
        uint32_t id = *(uint32_t *)arg;
        if (id == fail_remove) {
            errno = EBUSY;
            return -1;
        }
        if (!fbs[id].fb_id) {
            errno = ENOENT;
            return -1;
        }
        fbs[id].fb_id = 0;
        removals++;
        if (req == DRM_IOCTL_MODE_RMFB && id == displayed)
            displayed = 0;
        return 0;
    }
    if (req == DRM_IOCTL_MODE_SETCRTC) {
        struct drm_mode_crtc *c = arg;
        if (c->crtc_id != 50) {
            errno = EINVAL;
            return -1;
        }
        return present(c->fb_id, 1);
    }
    if (req == DRM_IOCTL_MODE_PAGE_FLIP) {
        struct drm_mode_crtc_page_flip_target *f = arg;
        last_flags = f->flags;
        last_data = f->user_data;
        flips++;
        return present(f->fb_id, 1);
    }
    if (req == DRM_IOCTL_MODE_SETPLANE) {
        struct drm_mode_set_plane *p = arg;
        last_plane = p->plane_id;
        return present(p->fb_id, p->plane_id == 60);
    }
    if (req == DRM_IOCTL_MODE_ATOMIC) {
        struct drm_mode_atomic *a = arg;
        uint32_t *objects = (void *)(uintptr_t)a->objs_ptr;
        uint32_t *counts = (void *)(uintptr_t)a->count_props_ptr;
        uint32_t *props = (void *)(uintptr_t)a->props_ptr;
        uint64_t *values = (void *)(uintptr_t)a->prop_values_ptr;
        last_flags = a->flags;
        last_data = a->user_data;
        commits++;
        unsigned at = 0;
        for (unsigned i = 0; i < a->count_objs; i++) {
            for (unsigned j = 0; j < counts[i]; j++)
                if (props[at + j] == 71 && (objects[i] == 60 || objects[i] == 61)) {
                    int r = present(values[at + j], objects[i] == 60);
                    if (r)
                        return r;
                }
            at += counts[i];
        }
        return 0;
    }
    errno = ENOTTY;
    return -1;
}
static struct drm_mode_fb_cmd2 add(int fd, uint32_t format)
{
    struct drm_mode_fb_cmd2 f = {.width = 1920,
                                 .height = 1080,
                                 .pixel_format = format,
                                 .handles = {42},
                                 .pitches = {1920 * 4 + 64},
                                 .offsets = {128}};
    assert(!adapter_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f));
    assert(fbs[f.fb_id].pixel_format == format);
    return f;
}
static int modeset(int fd, uint32_t fb, uint32_t crtc)
{
    struct drm_mode_crtc c = {.fb_id = fb, .crtc_id = crtc, .mode_valid = 1};
    int result = adapter_ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &c);
    assert(c.fb_id == fb); /* The caller's original FB identity is unchanged. */
    return result;
}
static void remove_original(int fd, uint32_t fb)
{
    assert(!adapter_ioctl(fd, DRM_IOCTL_MODE_RMFB, &fb));
    assert(!fbs[fb].fb_id);
}
struct flip_job {
    int fd;
    uint32_t fb;
};
static void *parallel_present(void *opaque)
{
    struct flip_job *job = opaque;
    for (unsigned i = 0; i < 64; i++)
        assert(!modeset(job->fd, job->fb, 50));
    return NULL;
}
static pthread_mutex_t cancel_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cancel_cond = PTHREAD_COND_INITIALIZER;
static int cancel_fd, close_entered, release_close, close_completed;
static int (*real_close_for_test)(int);
static int blocked_close(int fd)
{
    if (fd != cancel_fd)
        return real_close_for_test(fd);
    int state;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);
    assert(state == PTHREAD_CANCEL_DISABLE);
    pthread_mutex_lock(&cancel_lock);
    close_entered = 1;
    pthread_cond_broadcast(&cancel_cond);
    while (!release_close)
        pthread_cond_wait(&cancel_cond, &cancel_lock);
    pthread_mutex_unlock(&cancel_lock);
    int result = real_close_for_test(fd);
    close_completed = 1;
    errno = EDOM;
    return result;
}
static void *cancelled_close(void *unused)
{
    (void)unused;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    (void)adapter_close(cancel_fd);
    return NULL;
}
static unsigned close_range_calls;
static int (*real_close_range_for_test)(unsigned, unsigned, int);
static int counted_close_range(unsigned first, unsigned last, int flags)
{
    close_range_calls++;
    return real_close_range_for_test(first, last, flags);
}
int main(void)
{
    /* The preload is inert unless explicitly selected. */
    next_ioctl = kernel_ioctl;
    int fd = open("/dev/null", O_RDWR);
    assert(fd >= 0);
    struct drm_mode_fb_cmd2 unadapted = add(fd, DRM_FORMAT_ARGB8888);
    assert(modeset(fd, unadapted.fb_id, 50) == -1 && errno == EINVAL);
    assert(!descriptors);
    remove_original(fd, unadapted.fb_id);

    setenv("BEANJAR_NATIVE_KMS", "1", 1);
    setenv("LD_PRELOAD", "/usr/lib/libsystrim.so:/usr/lib/libkms-native-scanout.so /keep.so", 1);
    initialize();
    assert(enabled && !strcmp(getenv("QT_QPA_EGLFS_INTEGRATION"), "eglfs_kms_webos"));
    assert(!strcmp(getenv("WEBOS_COMPOSITOR_DISPLAYS"), "1"));
    assert(!strcmp(getenv("WEBOS_COMPOSITOR_EXTRA_WINDOWS"), "[]"));
    assert(!strcmp(getenv("LD_PRELOAD"), "/usr/lib/libsystrim.so /keep.so"));
    unsetenv("LD_PRELOAD");

    /* Dup before first AddFB2 shares one DRM-file bookkeeping group. */
    int alias_fd = adapter_dup(fd);
    assert(alias_fd >= 0);
    struct drm_mode_fb_cmd2 f = add(fd, DRM_FORMAT_ARGB8888);
    unsigned before = adds;
    struct drm_mode_set_plane cursor = {.plane_id = 61, .crtc_id = 50, .fb_id = f.fb_id};
    assert(!adapter_ioctl(fd, DRM_IOCTL_MODE_SETPLANE, &cursor));
    assert(displayed == f.fb_id && adds == before && last_plane == 61);
    assert(modeset(fd, f.fb_id, 51) == -1 && errno == EINVAL && adds == before);
    assert(!modeset(alias_fd, f.fb_id, 50));
    uint32_t view = displayed;
    assert(view != f.fb_id && adds == before + 1);
    struct drm_mode_fb_cmd2 expected = f;
    expected.fb_id = view;
    expected.pixel_format = DRM_FORMAT_XRGB8888;
    assert(!memcmp(&expected, &fbs[view], sizeof expected));
    assert(fbs[f.fb_id].pixel_format == DRM_FORMAT_ARGB8888);
    struct drm_mode_fb_cmd2 query = {.fb_id = view};
    assert(!adapter_ioctl(fd, DRM_IOCTL_MODE_GETFB2, &query));
    assert(query.pixel_format == DRM_FORMAT_XRGB8888);
    query.fb_id = f.fb_id;
    assert(!adapter_ioctl(fd, DRM_IOCTL_MODE_GETFB2, &query));
    assert(query.pixel_format == DRM_FORMAT_ARGB8888);

    struct drm_mode_crtc_page_flip_target flip = {.crtc_id = 50,
                                                  .fb_id = f.fb_id,
                                                  .flags = DRM_MODE_PAGE_FLIP_EVENT |
                                                           DRM_MODE_PAGE_FLIP_TARGET_ABSOLUTE,
                                                  .sequence = 19,
                                                  .user_data = UINT64_C(0x123456789abcdef0)};
    struct drm_mode_crtc_page_flip_target unchanged = flip;
    assert(!adapter_ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip));
    assert(displayed == view && adds == before + 1 && flips == 1);
    assert(last_flags == flip.flags && last_data == flip.user_data);
    assert(!memcmp(&flip, &unchanged, sizeof flip));
    fail_present = EIO;
    assert(adapter_ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) == -1 && errno == EIO);
    fail_present = 0;

    uint32_t objects[] = {50, 61, 60}, counts[] = {1, 1, 2}, props[] = {71, 71, 88, 71};
    uint64_t values[] = {f.fb_id, f.fb_id, 999, f.fb_id};
    uint64_t copy[4];
    memcpy(copy, values, sizeof copy);
    struct drm_mode_atomic atomic = {.count_objs = 3,
                                     .flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT,
                                     .objs_ptr = (uintptr_t)objects,
                                     .count_props_ptr = (uintptr_t)counts,
                                     .props_ptr = (uintptr_t)props,
                                     .prop_values_ptr = (uintptr_t)values,
                                     .user_data = UINT64_C(0xfeed112233445566)};
    assert(!adapter_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &atomic));
    assert(displayed == view && adds == before + 1 && commits == 1);
    assert(last_flags == atomic.flags && last_data == atomic.user_data);
    assert(!memcmp(values, copy, sizeof copy));

    /* Alias teardown failure leaves the original intact for a real retry. */
    fail_remove = view;
    assert(adapter_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == -1 && errno == EBUSY);
    assert(fbs[f.fb_id].fb_id && fbs[view].fb_id);
    fail_remove = f.fb_id;
    assert(adapter_ioctl(fd, DRM_IOCTL_MODE_RMFB, &f.fb_id) == -1 && errno == EBUSY);
    assert(fbs[f.fb_id].fb_id && !fbs[view].fb_id);
    fail_remove = 0;
    assert(!modeset(fd, f.fb_id, 50));
    view = displayed;
    remove_original(alias_fd, f.fb_id);
    assert(!fbs[view].fb_id);

    /* Successful close of one dup must retain the other descriptor's state. */
    f = add(alias_fd, DRM_FORMAT_ARGB8888);
    assert(!adapter_close(fd));
    assert(!modeset(alias_fd, f.fb_id, 50));
    remove_original(alias_fd, f.fb_id);
    int fd2 = adapter_fcntl(alias_fd, F_DUPFD_CLOEXEC, 0);
    assert(fd2 >= 0);
    assert(adapter_fcntl(fd2, F_GETFD) & FD_CLOEXEC);
    assert(adapter_dup2(alias_fd, alias_fd) == alias_fd);
    assert(adapter_dup2(alias_fd, fd2) == fd2);
    assert(adapter_dup3(alias_fd, fd2, O_CLOEXEC) == fd2);
    assert(!adapter_close(alias_fd));
    f = add(fd2, DRM_FORMAT_ARGB8888);
    fail_add = ENOMEM;
    before = adds;
    assert(modeset(fd2, f.fb_id, 50) == -1 && errno == ENOMEM && adds == before);
    fail_add = 0;
    assert(!modeset(fd2, f.fb_id, 50));
    view = displayed;
    struct drm_mode_closefb closefb = {.fb_id = f.fb_id};
    assert(!adapter_ioctl(fd2, DRM_IOCTL_MODE_CLOSEFB, &closefb));
    assert(!fbs[f.fb_id].fb_id && !fbs[view].fb_id && displayed == view);
    assert(!adapter_close(fd2) && !descriptors);

    /* Reusing the same fd and kernel FB number never resurrects an old alias. */
    int reused = open("/dev/null", O_RDWR);
    assert(reused >= 0);
    next_id = f.fb_id;
    f = add(reused, DRM_FORMAT_ARGB8888);
    before = adds;
    primary_argb = 1;
    assert(!modeset(reused, f.fb_id, 50) && adds == before && displayed == f.fb_id);
    primary_argb = 0;
    assert(!modeset(reused, f.fb_id, 50) && adds == before + 1);
    remove_original(reused, f.fb_id);
    f = add(reused, DRM_FORMAT_XRGB8888);
    before = adds;
    assert(!modeset(reused, f.fb_id, 50) && adds == before && displayed == f.fb_id);
    remove_original(reused, f.fb_id);

    /* The linear modifier is the same bytes as an implicit layout: viewed. */
    f = (struct drm_mode_fb_cmd2){.width = 64,
                                  .height = 64,
                                  .pixel_format = DRM_FORMAT_ARGB8888,
                                  .handles = {77},
                                  .pitches = {256},
                                  .flags = DRM_MODE_FB_MODIFIERS,
                                  .modifier = {DRM_FORMAT_MOD_LINEAR}};
    assert(!adapter_ioctl(reused, DRM_IOCTL_MODE_ADDFB2, &f));
    before = adds;
    assert(!modeset(reused, f.fb_id, 50) && adds == before + 1);
    remove_original(reused, f.fb_id);
    /* Any other explicit modifier remains the driver's contract and is not rewritten. */
    f = (struct drm_mode_fb_cmd2){.width = 64,
                                  .height = 64,
                                  .pixel_format = DRM_FORMAT_ARGB8888,
                                  .handles = {77},
                                  .pitches = {256},
                                  .flags = DRM_MODE_FB_MODIFIERS,
                                  .modifier = {fourcc_mod_code(ARM, 1)}};
    assert(!adapter_ioctl(reused, DRM_IOCTL_MODE_ADDFB2, &f));
    before = adds;
    assert(modeset(reused, f.fb_id, 50) == -1 && errno == EINVAL && adds == before);
    remove_original(reused, f.fb_id);
    /* Concurrent first presentations publish only one alias for the same FB. */
    f = add(reused, DRM_FORMAT_ARGB8888);
    before = adds;
    struct flip_job job = {reused, f.fb_id};
    pthread_t workers[4];
    for (unsigned i = 0; i < 4; i++)
        assert(!pthread_create(&workers[i], NULL, parallel_present, &job));
    for (unsigned i = 0; i < 4; i++)
        assert(!pthread_join(workers[i], NULL));
    assert(adds == before + 1);
    /* Cancellation while libc close is blocked is deferred until the real
     * close and metadata update complete and the adapter mutex is unlocked.
     */
    cancel_fd = adapter_dup(reused);
    assert(cancel_fd >= 0);
    real_close_for_test = next_close;
    next_close = blocked_close;
    pthread_t cancelled;
    assert(!pthread_create(&cancelled, NULL, cancelled_close, NULL));
    pthread_mutex_lock(&cancel_lock);
    while (!close_entered)
        pthread_cond_wait(&cancel_cond, &cancel_lock);
    assert(!pthread_cancel(cancelled));
    release_close = 1;
    pthread_cond_broadcast(&cancel_cond);
    pthread_mutex_unlock(&cancel_lock);
    void *joined = NULL;
    assert(!pthread_join(cancelled, &joined) && joined == PTHREAD_CANCELED);
    assert(close_completed && !find_descriptor(cancel_fd));
    next_close = real_close_for_test;
    /* This second operation would block forever if cancellation stranded lock. */
    assert(!modeset(reused, f.fb_id, 50));
    int old_state, restored_state;
    assert(!pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state));
    fail_present = EIO;
    assert(modeset(reused, f.fb_id, 50) == -1 && errno == EIO);
    fail_present = 0;
    assert(!pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &restored_state));
    assert(restored_state == PTHREAD_CANCEL_DISABLE);
    assert(!pthread_setcancelstate(old_state, NULL));
    remove_original(reused, f.fb_id);
    if (next_close_range) {
        int ranged = adapter_fcntl(reused, F_DUPFD_CLOEXEC, 100);
        assert(ranged >= 100);
        real_close_range_for_test = next_close_range;
        next_close_range = counted_close_range;
        assert(adapter_close_range(ranged, ranged, 1 << 1) == -1 && errno == EOPNOTSUPP);
        assert(adapter_close_range(ranged, ranged, (1 << 1) | (1 << 2)) == -1 &&
               errno == EOPNOTSUPP);
        assert(close_range_calls == 0 && find_descriptor(ranged));
        assert(adapter_fcntl(ranged, F_GETFD) >= 0);
        assert(!adapter_close_range(ranged, ranged, 1 << 2));
        assert(find_descriptor(ranged));
        assert(!adapter_close_range(ranged, ranged, 0));
        assert(!find_descriptor(ranged));
        assert(find_descriptor(reused) && close_range_calls == 2);
        next_close_range = real_close_range_for_test;
    }
    assert(!adapter_close(reused) && !descriptors);
    printf("native scanout: real FB views, primary provenance, metadata, atomic/legacy identity, "
           "errors, dup/close/reuse, cancellation, UNSHARE rejection and opt-in PASS\n");
    return 0;
}
