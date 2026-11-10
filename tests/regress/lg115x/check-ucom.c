// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* ARM32 compatibility/error probe for the O22 UCOM transport. Request layout
 * and payload bound: official webOS22 O2.0 include/ucom_kapi.h and
 * chip/o22/ucom/ucom_o22_func.c. State/reason are observations, not invented
 * expected hardware data. Native tvpowerd must separately prove ACTIVE.
 * Unsupported read 0xff follows lgemu's micom_answer default; it must leave
 * user memory unchanged. Invalid writes never reach the virtual micom. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

struct request {
    uint32_t selector, nvrw, nvaddr, nvdata, size;
    uint8_t valid, opcode, command, reserved;
    uint64_t buffer, user_buffer;
};
_Static_assert(sizeof(struct request) == 40, "ABI");
_Static_assert(offsetof(struct request, user_buffer) == 32, "pointer ABI");
#define RD _IOWR('u', 11, struct request)
#define WR _IOW('u', 10, struct request)
static int failures;

static void check(int fd, unsigned long cmd, struct request *r, int want, const char *label)
{
    int ret, error;

    errno = 0;
    ret = ioctl(fd, cmd, r);
    error = errno;
    printf("%s ret=%d errno=%d\n", label, ret, error);
    if ((!want && ret != 0) || (want && (ret != -1 || error != want)))
        failures++;
}

int main(void)
{
    uint8_t data = 0xff;
    struct request r = {
        .selector = 2,
        .size = 1,
        .command = 0xb1,
        .user_buffer = ((uint64_t)0xdeadbeef << 32) | (uintptr_t)&data,
    };
    int fd;

    if (sizeof(void *) != 4) {
        fprintf(stderr, "requires ARM32 userspace for compat probe\n");
        return 77;
    }
    fd = open("/dev/lg/ucom0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    check(fd, _IO('u', 0), NULL, 0, "init");
    check(fd, RD, &r, 0, "compat state read with poisoned pointer high word");
    printf("state=%u\n", data);
    r.command = 0x9a;
    check(fd, RD, &r, 0, "power reason read");
    printf("reason=0x%02x\n", data);
    r.size = 25;
    check(fd, RD, &r, EINVAL, "oversized payload");
    r.size = 1;
    r.user_buffer = 0;
    check(fd, WR, &r, EFAULT, "invalid write buffer");
    check(fd, _IO('u', 39), &r, ENOTTY, "unsupported ioctl");
    r.command = 0xff;
    r.user_buffer = (uintptr_t)&data;
    data = 0xa5;
    check(fd, RD, &r, EREMOTEIO, "unsupported micom read");
    printf("unsupported read buffer preserved=%s\n", data == 0xa5 ? "yes" : "no");
    if (data != 0xa5)
        failures++;
    close(fd);
    return failures ? 1 : 0;
}
