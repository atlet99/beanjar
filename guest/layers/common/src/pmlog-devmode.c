// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* usr/libexec/beanjar/pmlog-devmode: PmLogSetDevMode(1), for webos.pmlog_debug on the systemd
 * releases (rootfs/usr/libexec/beanjar/pmlog-debug).
 *
 * From webOS 5 on, libPmLogLib drops a client's line unless its context and message id are in the
 * library's own whitelist or dev mode is on. Dev mode is a
 * field of libPmLogLib's SysV shared globals, read on every message, so one call from any process
 * covers every client, running or not yet started. It lives only in that shared memory: nothing
 * persists, and PmLogDaemon calls PmLogSetDevMode itself only when configd's
 * system.collectDevLogs changes. LG's library, found at run time, is left as shipped. */
#include <dlfcn.h>
#include <stdio.h>

int main(void)
{
    void *lib = dlopen("libPmLogLib.so.3", RTLD_NOW);
    void (*set_dev_mode)(int) = lib ? (void (*)(int))dlsym(lib, "PmLogSetDevMode") : NULL;

    if (!set_dev_mode) {
        fprintf(stderr, "pmlog-devmode: no PmLogSetDevMode: %s\n", dlerror());
        return 1;
    }
    set_dev_mode(1);
    return 0;
}
