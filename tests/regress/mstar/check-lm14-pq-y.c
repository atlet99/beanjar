// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* Selected LM14 Y SRAM protocol; synthetic ten-byte coefficient rows. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
static void check(int ok, const char *why)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s\n", why);
        exit(1);
    }
}
static unsigned get(const void *p)
{
    unsigned v;
    __asm__ volatile("ldrh %0,[%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
}
static void put(void *p, unsigned v)
{
    __asm__ volatile("strh %0,[%1]" ::"r"(v), "r"(p) : "memory");
}
static void state(unsigned *writes, unsigned *reads, unsigned *crc)
{
    FILE *f = fopen("/proc/mstar-utopia/pq_y_sram", "r");
    check(f && fscanf(f, "writes=%u reads=%u crc32=%x", writes, reads, crc) == 3,
          "diagnostic state");
    fclose(f);
}
static void gamma_state(unsigned *writes, unsigned *crc, unsigned *dwrites, unsigned *dcrc)
{
    char line[256];
    FILE *f = fopen("/proc/mstar-utopia/pq_y_sram", "r");
    check(f != 0, "gamma state open");
    check(fgets(line, sizeof(line), f) && fgets(line, sizeof(line), f) &&
              fgets(line, sizeof(line), f) &&
              sscanf(line, "gamma_writes=%u gamma_crc32=%x degamma_writes=%u degamma_crc32=%x",
                     writes, crc, dwrites, dcrc) == 4,
          "gamma state");
    fclose(f);
}
static unsigned pattern_crc(unsigned count, unsigned bias)
{
    unsigned crc = ~0u;
    for (unsigned r = 0; r < count; r++)
        for (unsigned b = 0; b < 2; b++) {
            crc ^= ((r + bias) >> (b * 8)) & 255;
            for (unsigned k = 0; k < 8; k++)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
        }
    return ~crc;
}
int main(void)
{
    uint32_t map[] = {0x1f264000, 4096};
    const unsigned input[] = {0x70c, 0x710, 0x714, 0x724, 0x728};
    const unsigned output[] = {0x718, 0x71c, 0x720, 0x72c, 0x730};
    int fd = open("/webos/dev/miomap", O_RDWR);
    check(fd >= 0 && !ioctl(fd, 0x40084901, map), "device/setup");
    unsigned char *a = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    unsigned char *b = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    check(a != MAP_FAILED && b != MAP_FAILED, "aliases");
    unsigned writes, reads, crc, initial;
    state(&writes, &reads, &initial);
    check(!writes && !reads, "fresh controller");
    put(a + 0x708, 0);
    put(a + 0x704, 1);
    for (unsigned i = 0; i < 5; i++)
        put(a + input[i], 0x1234 + i);
    state(&writes, &reads, &crc);
    check(!writes && crc == initial, "no START no copy");
    put(a + 0x704, 0x100);
    check(get(b + 0x704) & 0x100, "disabled pending");
    put(a + 0x704, 0);
    put(a + 0x708, 512);
    put(a + 0x704, 0x101);
    check(get(b + 0x704) & 0x100, "invalid address pending");
    put(a + 0x704, 0);
    put(a + 0x708, 0);
    put(a + 0x704, 0x301);
    check((get(b + 0x704) & 0x300) == 0x300, "ambiguous request pending");
    put(a + 0x704, 0x501);
    check(get(b + 0x704) & 0x100, "reserved request pending");
    state(&writes, &reads, &crc);
    check(!writes && !reads && crc == initial, "invalid no state change");
    for (unsigned row = 0; row < 512; row++) {
        put(a + 0x704, 0);
        put(a + 0x708, row);
        for (unsigned i = 0; i < 5; i++)
            put(a + input[i], row * 5 + i);
        put(a + 0x704, 0x101);
        check(get(b + 0x704) == 1, "write completes after copy");
    }
    state(&writes, &reads, &crc);
    check(writes == 512 && !reads, "write count");
    printf("PQ synthetic rows CRC32=%08x\n", crc);
    for (unsigned row = 0; row < 512; row++) {
        put(a + 0x708, row);
        put(a + 0x704, 0x201);
        check(get(b + 0x704) == 1, "read completes after publication");
        for (unsigned i = 0; i < 5; i++) {
            check(get(b + output[i]) == row * 5 + i, "record readback");
            put(a + output[i], 0xffff);
            check(get(b + output[i]) == row * 5 + i, "hardware-owned read output");
        }
    }
    state(&writes, &reads, &crc);
    check(writes == 512 && reads == 512, "read count");
    put(a + 0x704, 0);
    put(a + 0x708, 511);
    put(a + 0x704, 0x201);
    check(get(b + output[4]) == 2559, "disable/re-enable retains SRAM");
    put(a + 0x704, 0);
    put(a + 0x708, 0x80);
    put(a + 0x704, 0x102);
    check(get(b + 0x704) == 0x102, "C address gap remains pending");
    put(a + 0x704, 0x103);
    check(get(b + 0x704) == 0x103, "ambiguous Y/C mode remains pending");
    for (unsigned row = 0; row < 256; row++) {
        unsigned address = row < 128 ? row : row + 256;
        put(a + 0x704, 0);
        put(a + 0x708, address);
        put(a + input[0], row * 3);
        put(a + input[1], row * 3 + 1);
        put(a + input[2], 0xff00 | (row & 255));
        put(a + 0x704, 0x102);
        check(get(b + 0x704) == 2, "C write completion");
    }
    for (unsigned row = 0; row < 256; row++) {
        put(a + 0x708, row < 128 ? row : row + 256);
        put(a + 0x704, 0x202);
        check(get(b + 0x704) == 2, "C read completion");
        check(get(b + output[0]) == row * 3 && get(b + output[1]) == row * 3 + 1 &&
                  (get(b + output[2]) & 255) == row,
              "C five-byte record aliases");
    }
    put(a + 0x708, 511);
    put(a + 0x704, 0x201);
    check(get(b + output[4]) == 2559 && get(b + output[0]) == 2555, "Y independent of C storage");
    unsigned gw, gc, dw, dc, zero;
    gamma_state(&gw, &zero, &dw, &dc);
    check(!gw && !dw, "fresh gamma storage");
    put(a + 0xbd8, 0);
    put(a + 0xbdc, 0x1234);
    put(a + 0xbd4, 0x60);
    gamma_state(&gw, &gc, &dw, &dc);
    check(!gw && gc == zero, "gamma no START no copy");
    put(a + 0xbd4, 8);
    check(get(b + 0xbd4) == 8, "gamma disabled pending");
    put(a + 0xbd8, 0x2b3);
    put(a + 0xbd4, 0x68);
    check(get(b + 0xbd4) == 0x68, "gamma overrun pending");
    put(a + 0xbd8, 0);
    put(a + 0xbd4, 0xe8);
    check(get(b + 0xbd4) == 0xe8, "gamma reserved pending");
    gamma_state(&gw, &gc, &dw, &dc);
    check(!gw && gc == zero, "gamma rejected no copy");
    for (unsigned r = 0; r < 0x2b3; r++) {
        put(a + 0xbd8, r);
        put(a + 0xbdc, r);
        put(a + 0xbd4, 0x68);
        check(get(b + 0xbd4) == 0x60, "gamma copied before completion");
    }
    for (unsigned r = 0; r < 256; r++) {
        put(a + 0xbe4, r);
        put(a + 0xbe8, r + 1000);
        put(a + 0xbe0, 0x68);
        check(get(b + 0xbe0) == 0x60, "degamma completion");
    }
    gamma_state(&gw, &gc, &dw, &dc);
    check(gw == 0x2b3 && dw == 256 && gc == pattern_crc(0x2b3, 0) && dc == pattern_crc(256, 1000),
          "gamma entire stored payload");
    printf("PQ gamma CRC32=%08x degamma CRC32=%08x\n", gc, dc);
    munmap(a, 4096);
    munmap(b, 4096);
    close(fd);
    puts("PQ Y/C/gamma SRAM transfer/readback/aliases/invalid/disable checks PASS");
    return 0;
}
