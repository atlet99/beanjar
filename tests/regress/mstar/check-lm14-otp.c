// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* Explicit unprovisioned virtual-device policy, not shipping OTP contents. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
static void check(int ok, const char *s)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s\n", s);
        exit(1);
    }
}
static unsigned get(const void *p)
{
    unsigned v;
    __asm__ volatile("ldrh %0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
}
static void put(void *p, unsigned v)
{
    __asm__ volatile("strh %0, [%1]" ::"r"(v), "r"(p) : "memory");
}
int main(void)
{
    uint32_t map[] = {0x1f000000, 0x230000};
    const unsigned status[] = {0x221584, 0x226784, 0x226f84, 0x226b84};
    int fd = open("/webos/dev/miomap", O_RDWR);
    check(fd >= 0 && !ioctl(fd, 0x40084901, map), "device/setup");
    unsigned char *a = mmap(0, map[1], PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    unsigned char *b = mmap(0, map[1], PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    check(a != MAP_FAILED && b != MAP_FAILED, "aliases");
    for (unsigned i = 0; i < 4; i++) {
        check(get(a + status[i]) == 0, "inactive status");
        put(a + status[i], 0xff00);
        check(get(b + status[i]) == 0, "clear alias synchronous");
        put(a + status[i], 0xffff);
        check(get(b + status[i]) == 0, "CPU cannot assert hardware status");
        unsigned v = 0xabcdff00;
        __asm__ volatile("str %0, [%1]" ::"r"(v), "r"(a + status[i]) : "memory");
        check(get(b + status[i]) == 0 && get(b + status[i] + 2) == 0xabcd,
              "word neighboring lanes");
        v = 0xff;
        __asm__ volatile("strb %0, [%1]" ::"r"(v), "r"(a + status[i] + 1) : "memory");
        check(get(b + status[i]) == 0, "byte clear");
    }
    for (unsigned bank = 0; bank < 2; bank++) {
        unsigned ctrl = bank ? 0x4098 : 0x4000, sel = bank ? 0x409c : 0x4004;
        put(a + sel, 0x7f);
        put(a + ctrl, 1);
        check(get(b + ctrl) & 1, "unknown remains pending");
        put(a + ctrl, 0);
        put(a + sel, bank ? 0x100 : 0x110);
        put(a + ctrl, 1);
        check(get(b + ctrl) & 1, "write mode remains pending");
        put(a + ctrl, 0);
        put(a + sel, bank ? 0x80 : 0x90);
        put(a + ctrl, 1);
        check(get(b + ctrl) & 1, "reserved remains pending");
        for (unsigned n = 0; n < (bank ? 36U : 40U); n++) {
            put(a + ctrl, 0);
            put(a + sel, bank ? n : n + 16);
            put(a + ctrl, 1);
            check(!(get(b + ctrl) & 1), "supported read completed");
            unsigned result = bank ? 0x4100 : 0x40c0;
            for (unsigned j = 0; j < 4; j++) {
                put(a + result + j * 4, 0xffff);
                check(get(b + result + j * 4) == 0, "blank hardware-owned result");
            }
        }
    }
    put(a + 0x4098, 0);
    put(a + 0x409c, 36);
    put(a + 0x4098, 1);
    check(!(get(b + 0x4098) & 1), "ADC distinct transaction");
    munmap(a, map[1]);
    munmap(b, map[1]);
    close(fd);
    puts("LM14 blank OTP/status aliases/read transport/unsupported/write-mode/lane checks PASS");
    return 0;
}
