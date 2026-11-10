// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libwayland-server-compat.so.0: the libwayland-server >= 1.2 functions Mesa's
 * EGL_WL_bind_wayland_display (src/egl/wayland/wayland-drm, gbm's wl_buffer import) calls,
 * implemented on top of the firmware's libwayland-server: 1.0.3 on webOS 1 (WT1H), 1.1 on
 * webOS 2 (W15M). Neither has these functions, and the two have the same public struct
 * wl_resource, struct wl_buffer, struct wl_object and wl_interface layouts (wayland-server.h,
 * wayland-util.h of tags 1.0.3 and 1.1.0), so one library serves both.
 *
 * The compositor (qtwayland 5.0 on webOS 1, Qt5Compositor 5.1 on webOS 2) must keep the
 * firmware's server, whose struct wl_resource is public and allocated by the caller. Mesa is
 * built against 1.26 headers and links this library ahead of libwayland-server (see
 * mesa/mesa-stack.sh), so these seven symbols bind here:
 *
 *   wl_resource_create, wl_resource_set_implementation, wl_resource_get_user_data,
 *   wl_resource_instance_of, wl_global_create, wl_global_destroy, wl_client_post_no_memory
 *
 * Everything else (wl_resource_post_event/_error/_no_memory, wl_resource_destroy) exists in
 * 1.0.3 and 1.1 with the same meaning.
 *
 * Two quirks of the old servers are handled here:
 *  - Signatures: wayland-scanner >= 1.2 prefixes a message's signature with its "since"
 *    version ("2nhiiuiiiiii" for wl_drm.create_prime_buffer). The 1.0.3 and 1.1 demarshallers
 *    (arg_count_for_signature) count the digit as an argument and assert on it. Interfaces
 *    passed in here are replaced by a copy with the digits stripped (argument types are
 *    unchanged, so types[] still lines up).
 *  - wl_buffer: in both, a buffer resource's `data` points at a
 *    struct wl_buffer { struct wl_resource resource; int32_t width, height; uint32_t busy_count; }
 *    that embeds the resource itself. qtwayland 5.0 (Surface::surface_attach) takes
 *    resource->data as the wl_buffer*, reads width/height at +0x28/+0x2c and the destroy
 *    signal at +0x18, and hands that pointer to eglCreateImageKHR(EGL_WAYLAND_BUFFER_WL);
 *    Qt5Compositor 5.1 (Surface::attach) keeps the wl_resource itself. Every resource
 *    allocated here has the wl_buffer tail. For wl_buffer resources `data` points back at the
 *    resource, width/height are copied from Mesa's struct wl_drm_buffer (wayland-drm.h:
 *    resource, drm, width, height, ...), and Mesa's own user data is kept on the side for
 *    wl_resource_get_user_data(). Either way the pointer Qt passes back to EGL is then the
 *    resource, which is what Mesa's wayland_drm_buffer_get() expects.
 *
 * Only the 32-bit ARM layout is targeted. No libwayland headers are used, so nothing from 1.26
 * leaks in: the 1.0.3/1.1 structures are declared below.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))

/* ---- Wayland 1.0.3 / 1.1 public ABI (wayland-util.h / wayland-server.h) ---- */
struct wl_message {
    const char *name;
    const char *signature;
    const struct wl_interface **types;
};
struct wl_interface {
    const char *name;
    int version;
    int method_count;
    const struct wl_message *methods;
    int event_count;
    const struct wl_message *events;
};
struct wl_list {
    struct wl_list *prev, *next;
};
struct wl_object {
    const struct wl_interface *interface;
    const void *implementation;
    uint32_t id;
};
struct wl_signal {
    struct wl_list listener_list;
};
struct wl_client;
struct wl_display;
struct wl_global;
struct wl_resource {
    struct wl_object object;
    void (*destroy)(struct wl_resource *resource);
    struct wl_list link;
    struct wl_signal destroy_signal;
    struct wl_client *client;
    void *data;
};
typedef void (*wl_global_bind_func_t)(struct wl_client *, void *, uint32_t, uint32_t);
typedef void (*wl_resource_destroy_func_t)(struct wl_resource *);

/* 1.0.3 / 1.1 functions this builds on */
uint32_t wl_client_add_resource(struct wl_client *client, struct wl_resource *resource);
struct wl_resource *wl_client_get_object(struct wl_client *client, uint32_t id);
void wl_resource_post_error(struct wl_resource *resource, uint32_t code, const char *msg, ...);
struct wl_global *wl_display_add_global(struct wl_display *display,
                                        const struct wl_interface *iface, void *data,
                                        wl_global_bind_func_t bind);
void wl_display_remove_global(struct wl_display *display, struct wl_global *global);

#define WL_DISPLAY_ERROR_NO_MEMORY 2
#define COMPAT_MAGIC               0x57434d50u /* "WCMP" */

/* A 1.0.3 / 1.1 struct wl_buffer, then our own bookkeeping. */
struct compat_resource {
    struct wl_resource r;
    int32_t width, height; /* struct wl_buffer tail, offsets 40/44 on ARM */
    uint32_t busy_count;
    uint32_t magic;
    int version;
    wl_resource_destroy_func_t user_destroy;
    void *user_data;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- interface sanitising (cache: original -> copy) ---- */
struct iface_copy {
    const struct wl_interface *orig;
    int version;
    struct wl_interface copy;
    struct iface_copy *next;
};
static struct iface_copy *ifaces;

static int has_digits(const struct wl_message *m, int n)
{
    for (int i = 0; i < n; i++)
        for (const char *s = m[i].signature; *s; s++)
            if (*s >= '0' && *s <= '9')
                return 1;
    return 0;
}

static struct wl_message *strip(const struct wl_message *m, int n)
{
    struct wl_message *c = calloc(n ? n : 1, sizeof *c);
    for (int i = 0; c && i < n; i++) {
        char *sig = strdup(m[i].signature), *d = sig;
        for (const char *s = m[i].signature; *s; s++)
            if (*s < '0' || *s > '9')
                *d++ = *s;
        *d = 0;
        c[i].name = m[i].name;
        c[i].signature = sig;
        c[i].types = m[i].types;
    }
    return c;
}

/* version < 0: keep the interface's own version */
static const struct wl_interface *sanitize(const struct wl_interface *iface, int version)
{
    if (!iface)
        return iface;
    if (version < 0 || version > iface->version)
        version = iface->version;
    if (version == iface->version && !has_digits(iface->methods, iface->method_count) &&
        !has_digits(iface->events, iface->event_count))
        return iface;
    pthread_mutex_lock(&lock);
    struct iface_copy *e;
    for (e = ifaces; e; e = e->next)
        if (e->orig == iface && e->version == version)
            break;
    if (!e && (e = calloc(1, sizeof *e))) {
        e->orig = iface;
        e->version = version;
        e->copy = *iface;
        e->copy.version = version;
        e->copy.methods = strip(iface->methods, iface->method_count);
        e->copy.events = strip(iface->events, iface->event_count);
        e->next = ifaces;
        ifaces = e;
    }
    pthread_mutex_unlock(&lock);
    return e ? &e->copy : iface;
}

static int iface_equal(const struct wl_interface *a, const struct wl_interface *b)
{
    return a == b || (a && b && strcmp(a->name, b->name) == 0);
}

/* ---- resources ---- */
static void compat_destroy(struct wl_resource *res)
{
    struct compat_resource *c = (struct compat_resource *)res;
    if (c->user_destroy)
        c->user_destroy(res);
    c->magic = 0;
    free(c);
}

EXPORT struct wl_resource *wl_resource_create(struct wl_client *client,
                                              const struct wl_interface *iface, int version,
                                              uint32_t id)
{
    struct compat_resource *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->r.object.interface = sanitize(iface, -1);
    c->r.object.id = id;
    c->r.destroy = compat_destroy;
    c->magic = COMPAT_MAGIC;
    c->version = version;
    /* 1.0.3 and 1.1 keep resource->link for the owner's lists; make it safe to wl_list_remove() */
    c->r.link.prev = c->r.link.next = &c->r.link;
    if (wl_client_add_resource(client, &c->r) == 0) {
        free(c);
        return NULL;
    }
    return &c->r;
}

EXPORT void wl_resource_set_implementation(struct wl_resource *res, const void *impl, void *data,
                                           wl_resource_destroy_func_t destroy)
{
    res->object.implementation = impl;
    res->data = data;
    struct compat_resource *c = (struct compat_resource *)res;
    if (c->magic != COMPAT_MAGIC) { /* not ours: best effort, like 1.0.3/1.1 */
        res->destroy = destroy;
        return;
    }
    c->user_destroy = destroy;
    c->user_data = data;
    if (data && res->object.interface && strcmp(res->object.interface->name, "wl_buffer") == 0) {
        /* Mesa's struct wl_drm_buffer { resource; drm; int32_t width, height; ... } */
        const int32_t *b = data;
        c->width = b[2];
        c->height = b[3];
        res->data =
            res; /* 1.0.3/1.1: data is the struct wl_buffer, which starts with the resource */
    }
}

EXPORT void *wl_resource_get_user_data(struct wl_resource *res)
{
    struct compat_resource *c = (struct compat_resource *)res;
    return c->magic == COMPAT_MAGIC ? c->user_data : res->data;
}

EXPORT int wl_resource_instance_of(struct wl_resource *res, const struct wl_interface *iface,
                                   const void *impl)
{
    return iface_equal(res->object.interface, iface) && res->object.implementation == impl;
}

EXPORT void wl_client_post_no_memory(struct wl_client *client)
{
    struct wl_resource *display = wl_client_get_object(client, 1); /* wl_display is object 1 */
    if (display)
        wl_resource_post_error(display, WL_DISPLAY_ERROR_NO_MEMORY, "no memory");
}

/* ---- globals: 1.0.3's and 1.1's wl_display_remove_global needs the display ---- */
struct global_entry {
    struct wl_global *g;
    struct wl_display *d;
    struct global_entry *next;
};
static struct global_entry *globals;

EXPORT struct wl_global *wl_global_create(struct wl_display *display,
                                          const struct wl_interface *iface, int version, void *data,
                                          wl_global_bind_func_t bind)
{
    /* 1.0.3 and 1.1 advertise interface->version, so the copy carries the requested version */
    struct wl_global *g = wl_display_add_global(display, sanitize(iface, version), data, bind);
    struct global_entry *e = g ? malloc(sizeof *e) : NULL;
    if (e) {
        pthread_mutex_lock(&lock);
        e->g = g;
        e->d = display;
        e->next = globals;
        globals = e;
        pthread_mutex_unlock(&lock);
    }
    return g;
}

EXPORT void wl_global_destroy(struct wl_global *g)
{
    struct global_entry **p, *e = NULL;
    pthread_mutex_lock(&lock);
    for (p = &globals; *p; p = &(*p)->next)
        if ((*p)->g == g) {
            e = *p;
            *p = e->next;
            break;
        }
    pthread_mutex_unlock(&lock);
    if (e) {
        wl_display_remove_global(e->d, g);
        free(e);
    }
}
