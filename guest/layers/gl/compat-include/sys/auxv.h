// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* glibc 2.13 has no <sys/auxv.h>/getauxval (added in 2.16). Implemented, hidden, in
 * glibc-compat.c by reading /proc/self/auxv. */
#ifndef _SYS_AUXV_H
#define _SYS_AUXV_H 1
#include <elf.h>
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((visibility("hidden"))) unsigned long getauxval(unsigned long type);
#ifdef __cplusplus
}
#endif
#endif
