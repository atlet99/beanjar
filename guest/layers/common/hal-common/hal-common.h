// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* guest/layers/common/hal-common: what the HAL shims and tvservice's private view of the OS
 * (guest/layers/common/tvservice) share, from the static library common/ (hal-log.c, physmem.c). */
#ifndef HAL_COMMON_H
#define HAL_COMMON_H
#include <stddef.h>

/* A line in syslog (/var/log/legacy-log), "tvservice-hal: ..." */
void hal_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* The emulator's physical memory (physmem.c): the pages a process that has the private view of
 * the OS (guest/layers/common/tvservice: tvservice, tvwin) reaches through /dev/mem. One instance
 * per process, in that view's library, which exports tvservice_physmem_alloc; the libdile_vt shim
 * looks it up at run time (libdile_vt/dile-common.h). */

/* A block of size bytes, zeroed, mapped at *virt; returns its physical address, 0 on failure. */
unsigned tvservice_physmem_alloc(unsigned size, void **virt);
/* A new file description of the physical memory, as open("/dev/mem", flags) gives; -1 (ENODEV)
 * without one. */
int physmem_open(int flags);
/* The /dev/gfx descriptor for a mapping of fd at [offset, offset + length) when fd is the physical
 * memory and the range lies in the graphics memory's (include/webos-gfx.h), else -1. */
int physmem_gfx_fd(int fd, unsigned long long offset, size_t length);
/* The display engine's /dev/lg/de0 for a mapping of fd at [offset, offset + length) when fd is the
 * physical memory and the range lies in the DE's memory (lg115x.ko's de_mem: the VTM page and the
 * VT frame buffers), else -1. */
int physmem_de_fd(int fd, unsigned long long offset, size_t length);
#endif
