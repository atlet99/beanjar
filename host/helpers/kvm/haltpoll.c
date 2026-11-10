// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Explicit host-QEMU per-VM halt polling; never preload into firmware. */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/kvm.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Assembly forwards x0/x1/x2 verbatim. This avoids va_arg on ioctl requests
 * whose caller provides no third argument; the kernel ignores that register
 * for such requests. No libc varargs forwarding or dlsym recursion occurs. */
__attribute__((visibility("hidden"))) int haltpoll_ioctl(int fd, unsigned long request,
                                                         unsigned long argument)
{
    long result = syscall(SYS_ioctl, fd, request, argument);
    if (request == KVM_CREATE_VM && result >= 0) {
        struct kvm_enable_cap cap = {.cap = KVM_CAP_HALT_POLL, .args = {0}};
        if (syscall(SYS_ioctl, (int)result, KVM_ENABLE_CAP, &cap) < 0) {
            int saved_errno = errno;
            fprintf(stderr, "KVM halt-poll: KVM_ENABLE_CAP failed errno=%d\n", saved_errno);
            close((int)result);
            errno = saved_errno;
            return -1;
        }
        fprintf(stderr, "KVM halt-poll: VM fd=%ld KVM_CAP_HALT_POLL=0 applied\n", result);
    }
    return (int)result;
}
