// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Hidden definitions of glibc > 2.13 symbols that the static libstdc++.a from
 * Debian gcc-12 (built against glibc 2.36) references. Linked as an object into
 * every Mesa/libdrm DSO so the output needs nothing above GLIBC_2.13/2.14. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/syscall.h>

#define HIDDEN __attribute__((visibility("hidden")))

/* 0 = "might be multi-threaded": libstdc++ then always uses atomics. */
HIDDEN char __libc_single_threaded = 0;

#ifndef SYS_getrandom
#define SYS_getrandom 384 /* ARM EABI */
#endif

HIDDEN int getentropy(void *buf, size_t len)
{
    if (len > 256) {
        errno = EIO;
        return -1;
    }
    long r = syscall(SYS_getrandom, buf, len, 0);
    if (r == (long)len)
        return 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, (char *)buf + off, len - off);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            close(fd);
            errno = EIO;
            return -1;
        }
        off += n;
    }
    close(fd);
    return 0;
}

HIDDEN uint32_t arc4random(void)
{
    uint32_t v = 0;
    getentropy(&v, sizeof v);
    return v;
}

HIDDEN void *aligned_alloc(size_t align, size_t size)
{
    return memalign(align, size);
}

/* getauxval (glibc 2.16): read /proc/self/auxv once. */
#include <elf.h>
HIDDEN unsigned long getauxval(unsigned long type);
HIDDEN unsigned long getauxval(unsigned long type)
{
    static Elf32_auxv_t aux[64];
    static int n = -1;
    if (n < 0) {
        int fd = open("/proc/self/auxv", O_RDONLY | O_CLOEXEC), got = 0;
        if (fd >= 0) {
            ssize_t r = read(fd, aux, sizeof aux);
            close(fd);
            if (r > 0)
                got = r / sizeof aux[0];
        }
        n = got;
    }
    for (int i = 0; i < n && aux[i].a_type != AT_NULL; i++)
        if (aux[i].a_type == type)
            return aux[i].a_un.a_val;
    errno = ENOENT;
    return 0;
}
