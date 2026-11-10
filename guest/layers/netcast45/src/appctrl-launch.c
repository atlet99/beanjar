// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Keep procmgr's PRE child PID and start stock appctrl immediately. Only its
 * genuine readiness call is gated, by the companion process-scoped preload. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *shim = "/etc/emulator/libappctrl-ready.so";
    const char *original = getenv("LD_PRELOAD");
    char pid[32];
    char *preload;
    void *library;
    (void)argc;

    /* A loader silently ignoring an unavailable LD_PRELOAD would bypass the
     * gate. Check that the companion can be loaded before executing appctrl. */
    library = dlopen(shim, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "appctrl-launch: cannot load readiness shim: %s\n", dlerror());
        return 1;
    }
    dlclose(library);
    snprintf(pid, sizeof(pid), "%ld", (long)getpid());
    preload = malloc(strlen(shim) + (original ? strlen(original) : 0) + 2);
    if (!preload) {
        perror("appctrl-launch: allocate preload");
        return 1;
    }
    strcpy(preload, shim);
    if (original && *original) {
        strcat(preload, ":");
        strcat(preload, original);
    }
    if ((original ? setenv("BEANJAR_APPCTRL_PRELOAD", original, 1)
                  : unsetenv("BEANJAR_APPCTRL_PRELOAD")) ||
        setenv("BEANJAR_APPCTRL_READY_PID", pid, 1) || setenv("LD_PRELOAD", preload, 1)) {
        perror("appctrl-launch: configure preload");
        free(preload);
        return 1;
    }
    free(preload);
    argv[0] = "/usr/local/bin/appctrl";
    execv(argv[0], argv);
    perror("appctrl-launch: exec appctrl");
    return 1;
}
