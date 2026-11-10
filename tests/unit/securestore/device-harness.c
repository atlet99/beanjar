// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Compile the genuine kernel handler with controlled allocation, privilege and user-copy calls.
 * Real kernel execution, compat and jail permissions are covered by test-sestore-vm.py. */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../include/webos-sestore.h"
typedef uint8_t u8;
typedef uint32_t u32;
#define __user
#define CAP_SYS_ADMIN      21
#define GFP_KERNEL         0
#define DEFINE_MUTEX(name) int name
static void mutex_lock(int *lock)
{
    (void)lock;
}
static void mutex_unlock(int *lock)
{
    (void)lock;
}
static int administrator = 1, present = 1;
static int capable(int capability)
{
    (void)capability;
    return administrator;
}
static void *kzalloc(size_t size, int flags)
{
    (void)flags;
    return calloc(1, size);
}
static void *kvmalloc(size_t size, int flags)
{
    (void)flags;
    return malloc(size);
}
static void kvfree_sensitive(void *p, size_t size)
{
    if (p) {
        memset(p, 0, size);
        free(p);
    }
}
static void kfree_sensitive(void *p)
{
    free(p);
}
static void *u64_to_user_ptr(uint64_t pointer)
{
    return (void *)(uintptr_t)pointer;
}
static int user_copy(void *to, const void *from, size_t size)
{
    if ((uintptr_t)to < 4096 || (uintptr_t)from < 4096)
        return 1;
    memcpy(to, from, size);
    return 0;
}
#define copy_from_user user_copy
#define copy_to_user   user_copy
static u32 get_unaligned_le32(const void *pointer)
{
    const u8 *p = pointer;
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static void *memchr_inv(const void *pointer, int value, size_t size)
{
    const u8 *p = pointer;
    for (size_t i = 0; i < size; ++i)
        if (p[i] != value)
            return (void *)(p + i);
    return NULL;
}
struct file {
    int unused;
};
#include "kernel-test-section.h"

void test_reset(void)
{
    store_free(snapshot);
    snapshot = NULL;
    present = 1;
    administrator = 1;
}
void test_present(int value)
{
    present = value;
}
int test_load(const void *data, unsigned int bytes, int privileged)
{
    struct webos_sestore_load load = {.data = (uintptr_t)data, .bytes = bytes};
    administrator = privileged;
    int ret = (int)store_load((uintptr_t)&load);
    administrator = 1;
    return ret;
}
int emulator_test_open(const char *path, int flags, ...)
{
    (void)flags;
    if (strcmp(path, WEBOS_SESTORE_DEVICE) || !present) {
        errno = ENOENT;
        return -1;
    }
    return 12345;
}
int emulator_test_close(int fd)
{
    (void)fd;
    return 0;
}
int emulator_test_ioctl(int fd, unsigned long cmd, ...)
{
    (void)fd;
    va_list args;
    va_start(args, cmd);
    void *request = va_arg(args, void *);
    va_end(args);
    int ret = (int)store_ioctl(NULL, (unsigned int)cmd, (uintptr_t)request);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
