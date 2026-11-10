// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Print the core wl_*_interface tables a Wayland library exports (name, version, requests,
 * events with signatures), to compare the firmware's webOS-patched protocol with upstream.
 *   wl-iface-dump /usr/lib/libwayland-client.so.0.1.0 */
#include <dlfcn.h>
#include <stdio.h>

struct wl_message {
    const char *name, *signature;
    const void **types;
};
struct wl_interface {
    const char *name;
    int version;
    int nm;
    const struct wl_message *m;
    int ne;
    const struct wl_message *e;
};

static const char *names[] = {"display",
                              "registry",
                              "callback",
                              "compositor",
                              "shm_pool",
                              "shm",
                              "buffer",
                              "data_offer",
                              "data_source",
                              "data_device",
                              "data_device_manager",
                              "shell",
                              "shell_surface",
                              "surface",
                              "seat",
                              "pointer",
                              "keyboard",
                              "touch",
                              "output",
                              "region",
                              "subcompositor",
                              "subsurface",
                              "fixes",
                              NULL};

int main(int argc, char **argv)
{
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    for (const char **n = names; *n; n++) {
        char sym[64];
        snprintf(sym, sizeof sym, "wl_%s_interface", *n);
        const struct wl_interface *i = dlsym(h, sym);
        if (!i)
            continue;
        printf("%s v%d\n", i->name, i->version);
        for (int k = 0; k < i->nm; k++)
            printf("  req %d %s(%s)\n", k, i->m[k].name, i->m[k].signature);
        for (int k = 0; k < i->ne; k++)
            printf("  evt %d %s(%s)\n", k, i->e[k].name, i->e[k].signature);
    }
    return 0;
}
