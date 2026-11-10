// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libpmlog-keep-all.so: PmLogDaemon without its message whitelist.
 *
 * The release PmLogDaemon drops every message whose "CONTEXT/MSGID" is not in
 * /etc/PmLogDaemon/whitelist.txt: debug and freetext messages, plain syslog(), and the per-app
 * per-app files in /var/log all stay empty. Its LogMessage looks the key up with
 * g_hash_table_lookup and returns on NULL; the value found is only tested for NULL. That is the
 * executable's only call of g_hash_table_lookup (WT1H 05.06.10.01, W15M 04.06.75.01, W16R
 * 05.70.50.01).
 *
 * This library, LD_PRELOADed into PmLogDaemon by the override guest/initramfs/rootfs/init writes
 * for webos.pmlog=full, defines g_hash_table_lookup: it calls glib's, and when that finds nothing
 * for a call made from the executable itself (the return address inside one of the main
 * program's loaded segments, the first object dl_iterate_phdr reports), it answers a non-NULL
 * sentinel instead. Calls from glib and every other library get glib's answer. A daemon that
 * made no such call would log as stock. The constructor takes the library out of LD_PRELOAD
 * again, so what the daemon starts sees the stock job's environment.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SELF         "/usr/lib/libpmlog-keep-all.so"
#define MAX_SEGMENTS 8

static struct {
    uintptr_t start, end;
} exe[MAX_SEGMENTS];
static int exe_segments;

static int first_object(struct dl_phdr_info *info, size_t size, void *data)
{
    int i;
    (void)size;
    (void)data;
    for (i = 0; i < info->dlpi_phnum && exe_segments < MAX_SEGMENTS; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_X))
            continue;
        exe[exe_segments].start = info->dlpi_addr + ph->p_vaddr;
        exe[exe_segments].end = info->dlpi_addr + ph->p_vaddr + ph->p_memsz;
        exe_segments++;
    }
    return 1; /* the main program only */
}

/* Out of LD_PRELOAD, in place (as guest/layers/common/tvservice's libtvservice-private.so does). */
static void leave_preload(void)
{
    char *v = getenv("LD_PRELOAD"), *p;
    size_t n = strlen(SELF);
    if (!v)
        return;
    for (p = v; (p = strstr(p, SELF)); p++) {
        if ((p == v || p[-1] == ' ' || p[-1] == ':') && (!p[n] || p[n] == ' ' || p[n] == ':')) {
            char *from = p + n;
            if (p > v && !*from)
                p--;
            else if (*from)
                from++;
            memmove(p, from, strlen(from) + 1);
            break;
        }
    }
    if (!v[strspn(v, " :")])
        unsetenv("LD_PRELOAD");
}

__attribute__((constructor)) static void init(void)
{
    leave_preload();
    dl_iterate_phdr(first_object, NULL);
}

static int from_exe(uintptr_t ret)
{
    int i;
    for (i = 0; i < exe_segments; i++)
        if (ret >= exe[i].start && ret < exe[i].end)
            return 1;
    return 0;
}

typedef void *lookup_fn(void *, const void *);

__attribute__((visibility("default"))) void *g_hash_table_lookup(void *table, const void *key)
{
    static lookup_fn *real;
    static const char sentinel[] = "pmlog-keep-all";
    void *found;
    if (!real && !(real = (lookup_fn *)dlsym(RTLD_NEXT, "g_hash_table_lookup")))
        return NULL;
    found = real(table, key);
    if (!found && from_exe((uintptr_t)__builtin_return_address(0)))
        return (void *)sentinel;
    return found;
}
