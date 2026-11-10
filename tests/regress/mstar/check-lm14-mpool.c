// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
struct legacy {
    uint32_t addr, size, interval;
    uint8_t miu, pad[3];
};
static int set(int fd, struct legacy p, int expected)
{
    errno = 0;
    int rc = ioctl(fd, 0x80104d04, &p);
    if ((expected == 0 && rc == 0) || (expected && rc == -1 && errno == expected))
        return 0;
    fprintf(stderr, "SET addr=%x size=%x miu=%u rc=%d errno=%d expected=%d\n", p.addr, p.size,
            p.miu, rc, errno, expected);
    return 1;
}
int main(void)
{
    int a = open("/dev/malloc", O_RDWR), b = open("/dev/malloc", O_RDWR);
    if (a < 0 || b < 0) {
        perror("malloc");
        return 1;
    }
    /* Selected libMsOS passes global PA, including its MIU1 interval. */
    struct legacy p = {0x80001000, 4096, 0x80000000, 1, {0}};
    if (set(a, p, 0) || set(b, p, 0))
        return 1;
    volatile uint32_t *x = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, a, 0);
    volatile uint32_t *y = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, b, 0);
    if (x == MAP_FAILED || y == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    x[0] = 0x12345678;
    if (y[0] != x[0])
        return 2;
    y[1] = 0x87654321;
    if (x[1] != y[1])
        return 3;
    munmap((void *)x, 4096);
    munmap((void *)y, 4096);
    p.addr = 0x1000;
    if (set(a, p, EINVAL))
        return 1;
    p.addr = 0xbffff000;
    p.size = 8192;
    if (set(a, p, EINVAL))
        return 1;
    p.addr = 0xbffff000;
    p.size = 4096;
    if (set(a, p, 0))
        return 1;
    p.miu = 2;
    if (set(a, p, EINVAL))
        return 1;
    p.miu = 0;
    p.addr = 0x80000000;
    if (set(a, p, EINVAL))
        return 1;
    p.addr = 0x1000;
    p.interval = 0x40000000;
    if (set(a, p, EINVAL))
        return 1;
    close(a);
    close(b);
    puts("LM14 MIU1 aliases and SET boundaries PASS");
    return 0;
}
