// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
static int wait_done(volatile uint8_t *p, int expected)
{
    volatile uint16_t *ctrl = (volatile uint16_t *)(p + 0x2be8);
    for (int i = 0; i < 100; i++) {
        if ((*ctrl & 8) != 0) {
            if (expected)
                return 0;
            return 1;
        }
        usleep(1000);
    }
    return expected ? 1 : 0;
}
static int mailbox(volatile uint8_t *p, uint16_t control, uint32_t command, int expected)
{
    volatile uint16_t *lo = (volatile uint16_t *)(p + 0x2b00);
    volatile uint16_t *hi = (volatile uint16_t *)(p + 0x2b04);
    volatile uint16_t *data_lo = (volatile uint16_t *)(p + 0x2c00);
    volatile uint16_t *data_hi = (volatile uint16_t *)(p + 0x2c04);
    *lo = *hi = 0;
    *(volatile uint16_t *)(p + 0x2be8) = control;
    *data_lo = *data_hi = 0;
    *lo = command;
    *hi = command >> 16;
    for (int i = 0; i < 100; i++) {
        if (!*lo && !*hi) {
            int correct = expected && (command >> 24 != 1 || (!*data_lo && *data_hi == 0x100));
            return correct ? 0 : 1;
        }
        usleep(1000);
    }
    if (!expected && !*data_lo && !*data_hi)
        return 0;
    return 1;
}
static int state(unsigned loaded, unsigned running, unsigned pending, unsigned high,
                 unsigned configured, unsigned base)
{
    FILE *f = fopen("/proc/mstar-utopia/tsp", "r");
    unsigned l, r, p, h, c, b;
    int ok = f &&
             fscanf(f, "loaded=%u running=%u pending=%u high=%x configured=%u base=%x", &l, &r, &p,
                    &h, &c, &b) == 6 &&
             l == loaded && r == running && p == pending && h == high && c == configured &&
             b == base;
    if (f)
        fclose(f);
    if (!ok)
        fprintf(stderr, "TSP modeled state mismatch\n");
    return !ok;
}
int main(int argc, char **argv)
{
    int fd = open("/webos/dev/miomap", O_RDWR);
    uint32_t map[] = {0x1f200000, 0xe0000};
    if (fd < 0 || ioctl(fd, 0x40084901, map)) {
        perror("miomap");
        return 1;
    }
    volatile uint8_t *p = mmap(0, map[1], PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "watch")) {
        for (unsigned i = 0; i < 20000; i++) {
            uint32_t cmd = *(volatile uint16_t *)(p + 0x2b00) |
                           ((uint32_t)*(volatile uint16_t *)(p + 0x2b04) << 16);
            if (cmd) {
                uint32_t data = *(volatile uint16_t *)(p + 0x2c00) |
                                ((uint32_t)*(volatile uint16_t *)(p + 0x2c04) << 16);
                printf("MCU_Cmd=%08x MCU_Data0=%08x TSP_Ctrl=%04x\n", cmd, data,
                       *(volatile uint16_t *)(p + 0x2be8));
                break;
            }
            usleep(50);
        }
    }
    if (argc > 1 && !strcmp(argv[1], "selftest")) {
        if (mailbox(p, 3, 0x01000000, 0) || state(0, 0, 0, 0, 0, 0))
            return 10; /* no downloaded image */
        int mem = open("/webos/dev/malloc", O_RDWR);
        struct {
            uint32_t addr, size, interval;
            uint8_t miu, pad[3];
        } info = {0x1b4c0000, 0x4000, 0x80000000, 0, {0}};
        if (mem < 0 || ioctl(mem, 0x80104d04, &info))
            return 6;
        volatile uint8_t *ram = mmap(0, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, mem, 0);
        if (ram == MAP_FAILED)
            return 7;
        for (unsigned i = 0; i < info.size; i++)
            ram[i] = i & 255;
        volatile uint16_t *ctrl = (volatile uint16_t *)(p + 0x2be8);
        *ctrl = 0;
        *(volatile uint16_t *)(p + 0x2b80) = 0;
        *(volatile uint16_t *)(p + 0x2b84) = 0;
        *(volatile uint16_t *)(p + 0x2be0) = 0xffff;
        *(volatile uint16_t *)(p + 0x2c28) = 0xff;
        *(volatile uint16_t *)(p + 0x2be4) = 0x1000;
        *ctrl = 4;
        if (wait_done(p, 0))
            return 2;
        *ctrl = 0;
        *(volatile uint16_t *)(p + 0x2be0) = 0x4c00;
        *(volatile uint16_t *)(p + 0x2c28) = 0x1b;
        *(volatile uint16_t *)(p + 0x2be4) = 0x1001;
        *ctrl = 4;
        if (wait_done(p, 0))
            return 3;
        *ctrl = 0;
        *(volatile uint16_t *)(p + 0x2be4) = 0x1000;
        if (wait_done(p, 0))
            return 4;
        *ctrl = 0x1004;
        if (wait_done(p, 1) || *ctrl != 0x100c)
            return 5;
        *ctrl = 0;
        if (wait_done(p, 0))
            return 8;
        if (mailbox(p, 0, 0x01000000, 0) || mailbox(p, 2, 0x01000000, 0))
            return 11; /* disabled and reset */
        if (mailbox(p, 3, 0x01000000, 1) || mailbox(p, 3, 0x02000000, 0))
            return 12; /* supported ALIVE, unknown command */
        if (mailbox(p, 0, 0x01000000, 0) || mailbox(p, 3, 0x01000000, 1))
            return 13; /* stop/re-enable retains QMEM */
        if (mailbox(p, 3, 0x06000000, 0) || mailbox(p, 3, 0x0500ffff, 1) ||
            state(1, 1, 1, 0xffff, 0, 0) || mailbox(p, 3, 0x0600ffff, 0) ||
            state(1, 1, 1, 0xffff, 0, 0))
            return 15; /* missing high half and out-of-record address */
        if (mailbox(p, 3, 0x05001b4c, 1) || mailbox(p, 3, 0x06000001, 1) ||
            state(1, 1, 0, 0, 1, 0x1b4c0001))
            return 16; /* commands carry no alignment constraint */
        if (mailbox(p, 3, 0x05001b4c, 1) || mailbox(p, 0, 0x06000000, 0) ||
            mailbox(p, 3, 0x06000000, 0) || state(1, 1, 0, 0, 0, 0))
            return 17; /* disable invalidates the pending address half */
        if (mailbox(p, 3, 0x05001b4c, 1) || mailbox(p, 3, 0x06000000, 1) ||
            state(1, 1, 0, 0, 1, 0x1b4c0000))
            return 18;
        *(volatile uint16_t *)(p + 0x2b00) = 0;
        *(volatile uint16_t *)(p + 0x2b04) = 0;
        for (unsigned i = 0; i < info.size; i++)
            ram[i] = (i & 255) ^ 0x55;
        *ctrl = 4;
        if (wait_done(p, 1))
            return 9;
        if (mailbox(p, 3, 0x01000000, 1))
            return 14;
        *ctrl = 0;
        munmap((void *)ram, info.size);
        close(mem);
        puts("TSP DMA source/length/start gating, reset, repeat and preserved "
             "control bits PASS");
        puts("TSP DRAM high/low sequencing, address validation and reset gating "
             "PASS");
        puts("TSP ALIVE requires downloaded QMEM and running service; "
             "reset/off/unknown gating PASS");
    }
    for (unsigned off = 0x180; off <= 0x22c; off += 4)
        printf("TSP+%03x=%04x\n", off, *(volatile uint16_t *)(p + 0x2a00 + off));
    munmap((void *)p, map[1]);
    close(fd);
    return 0;
}
