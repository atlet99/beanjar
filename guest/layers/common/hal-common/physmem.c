// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* guest/layers/common/hal-common: the emulator's physical memory, behind /dev/mem in the processes
 * that have tvservice's private view of the OS (guest/layers/common/tvservice/libc-private.c, which
 * opens and maps it through physmem_open and physmem_gfx_fd).
 *
 * LG's code (tvservice's capture, libhal_lg115x, libkadaptor, liblxv, libhalgal) opens /dev/mem to
 * map LG's register windows and frame buffers. In the VM those physical addresses are either
 * unbacked (TCG may raise an external abort) or QEMU virt's PCIe MMIO window, where the virtio
 * devices are. This is a private "physical memory" instead: a sparse 4 GiB file on tmpfs, created
 * and unlinked at the first use, so a mapping at any offset reads 0 where nothing was written (as
 * /dev/zero did in the observation boots) and every mapping of an address sees the same pages.
 * The frame buffers the emulator's DILE_VT hands out (libdile_vt) are allocated in it, at
 * PHYS_BASE, so tvservice's own mmap of their physical addresses reaches the same pixels
 * (tvservice_physmem_alloc). The graphics memory's range (include/webos-gfx.h) is /dev/gfx, as on
 * the TV, where the gfx driver's memory is a DDR range: a mapping of it goes to the same pages as
 * gm's and libgm's (libdile_gal), so tv.capture's scaler maps its DILE_GAL surfaces at their
 * physicalAddress (physmem_gfx_fd). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hal-common.h"
#include "../../../../include/webos-gfx.h"

#define PHYS_SPACE (1ULL << 32) /* the file spans the 32-bit physical address space */
/* PHYS_BASE: the emulator's frame buffers, PHYS_SIZE from there, in a hole of the SoC's physical
 * address space between its DDR and its registers: the chip's soc_phys_base
 * (guest/layers/soc/<chip>/meson.build), given by hal-common/meson.build. */
#ifndef PHYS_BASE
#error "PHYS_BASE: the chip's soc_phys_base"
#endif
#define PHYS_SIZE   (64u << 20)
#define PHYS_ALIGN  0x10000u
#define PHYS_BLOCKS 32

/* libc's own open: the view's open/open64 are the process's (libc-private.c) */
int __open64(const char *path, int flags, ...);

static int phys_fd = -1;
static pthread_once_t phys_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t phys_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    unsigned phys, size;
    void *virt;
} blocks[PHYS_BLOCKS];

static void phys_init(void)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/.tvservice-physmem.%d", (int)getpid());
    phys_fd = __open64(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (phys_fd < 0) {
        hal_log("physical memory: %s: %s", path, strerror(errno));
        return;
    }
    unlink(path);
    if (ftruncate64(phys_fd, (off64_t)PHYS_SPACE) < 0) {
        hal_log("physical memory: ftruncate: %s", strerror(errno));
        close(phys_fd);
        phys_fd = -1;
    }
}

int physmem_open(int flags)
{
    char self[32];
    pthread_once(&phys_once, phys_init);
    if (phys_fd < 0) {
        errno = ENODEV;
        return -1;
    }
    snprintf(self, sizeof self, "/proc/self/fd/%d", phys_fd);
    return __open64(self, (flags & ~O_CREAT) | O_CLOEXEC, 0);
}

unsigned tvservice_physmem_alloc(unsigned size, void **virt)
{
    unsigned phys = PHYS_BASE, i, slot = PHYS_BLOCKS;
    void *p;
    pthread_once(&phys_once, phys_init);
    if (phys_fd < 0 || !size)
        return 0;
    size = (size + PHYS_ALIGN - 1) & ~(PHYS_ALIGN - 1);
    pthread_mutex_lock(&phys_lock);
    /* first fit: the lowest address past every block it would overlap */
    for (int moved = 1; moved;) {
        moved = 0;
        for (i = 0; i < PHYS_BLOCKS; i++)
            if (blocks[i].size && phys < blocks[i].phys + blocks[i].size &&
                blocks[i].phys < phys + size) {
                phys = blocks[i].phys + blocks[i].size;
                moved = 1;
            }
    }
    for (i = 0; i < PHYS_BLOCKS && slot == PHYS_BLOCKS; i++)
        if (!blocks[i].size)
            slot = i;
    if (slot == PHYS_BLOCKS || phys + size > PHYS_BASE + PHYS_SIZE) {
        pthread_mutex_unlock(&phys_lock);
        hal_log("physical memory: no room for %u bytes", size);
        return 0;
    }
    p = mmap64(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, phys_fd, (off64_t)phys);
    if (p == MAP_FAILED) {
        pthread_mutex_unlock(&phys_lock);
        hal_log("physical memory: mmap at 0x%08x: %s", phys, strerror(errno));
        return 0;
    }
    memset(p, 0, size);
    blocks[slot].phys = phys;
    blocks[slot].size = size;
    blocks[slot].virt = p;
    pthread_mutex_unlock(&phys_lock);
    if (virt)
        *virt = p;
    return phys;
}

/* ---- the graphics memory in the physical address space ---- */

static int gfx_fd = -1;
static pthread_once_t gfx_once = PTHREAD_ONCE_INIT;

static void gfx_open(void)
{
    gfx_fd = __open64(WEBOS_GFX_PATH, O_RDWR | O_CLOEXEC);
    if (gfx_fd < 0)
        hal_log("physical memory: %s: %s", WEBOS_GFX_PATH, strerror(errno));
}

int physmem_gfx_fd(int fd, unsigned long long offset, size_t length)
{
    struct stat64 a, b;
    if (phys_fd < 0 || offset < WEBOS_GFX_PHYS_BASE ||
        offset + length > (unsigned long long)WEBOS_GFX_PHYS_BASE + WEBOS_GFX_MEM_SIZE ||
        fstat64(fd, &a) < 0 || fstat64(phys_fd, &b) < 0 || a.st_dev != b.st_dev ||
        a.st_ino != b.st_ino)
        return -1;
    pthread_once(&gfx_once, gfx_open);
    return gfx_fd;
}
