// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* Selected LM14 user-mode HWI2C master registers (lm14_hwi2c=1). Load the module with
 * i2c=micom:0x0052,amp:0x0254: lgemu's micom on port index 0 (8-bit 0x52) and its amplifier on
 * port index 2 (0x54). Byte addresses as libdrvHWI2C's HAL_HWI2C_* use them. */
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
static void check(int ok, const char *why)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s\n", why);
        exit(1);
    }
}
static unsigned getb(const volatile uint8_t *p)
{
    unsigned v;
    __asm__ volatile("ldrb %0,[%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
}
static void putb(volatile uint8_t *p, unsigned v)
{
    __asm__ volatile("strb %0,[%1]" ::"r"(v), "r"(p) : "memory");
}
static unsigned geth(const volatile uint8_t *p)
{
    unsigned v;
    __asm__ volatile("ldrh %0,[%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
}
static void puth(volatile uint8_t *p, unsigned v)
{
    __asm__ volatile("strh %0,[%1]" ::"r"(v), "r"(p) : "memory");
}
static volatile uint8_t *a, *b;
/* a register byte address of port window w (0x000: port 0, 0x400: port 2) */
static volatile uint8_t *R(volatile uint8_t *m, unsigned w, unsigned reg)
{
    return m + w + 2 * (reg & ~1u) + (reg & 1);
}
enum {
    CTRL = 0,
    START = 2,
    STOP = 3,
    WDATA = 4,
    NACK = 5,
    RDATA = 6,
    RECV = 7,
    INT = 8,
    STATE = 0x0a
};
static int flag(unsigned w)
{
    return getb(R(b, w, INT)) & 1;
}
static void clear(unsigned w)
{
    putb(R(a, w, INT), 1);
    check(!flag(w), "interrupt flag cleared by writing 1");
}
static int send(unsigned w, unsigned byte)
{
    putb(R(a, w, WDATA), byte);
    check(flag(w), "a sent byte raises the flag");
    clear(w);
    return !(getb(R(b, w, NACK)) & 1);
}
static void start(unsigned w)
{
    putb(R(a, w, START), 1);
    check(flag(w) && (getb(R(b, w, STATE)) & 31) != 0, "START: flag, state not idle");
    clear(w);
}
static void stop(unsigned w)
{
    putb(R(a, w, STOP), 1);
    check(flag(w) && (getb(R(b, w, STATE)) & 31) == 0, "STOP: flag, idle");
    clear(w);
}
static unsigned recv(unsigned w, int last)
{
    putb(R(a, w, RECV), last ? 3 : 1);
    check(flag(w), "a receive raises the flag");
    clear(w);
    return getb(R(b, w, RDATA));
}
static void transfers(unsigned port, unsigned *t, unsigned *n)
{
    char line[160];
    FILE *f = fopen("/proc/mstar-utopia/hwi2c", "r");
    check(f != 0, "state open");
    for (unsigned i = 0; i <= port; i++)
        check(fgets(line, sizeof(line), f) != 0, "state line");
    fclose(f);
    check(sscanf(strstr(line, "transfers"), "transfers %u nacks %u", t, n) == 2, "state counters");
}
static void upper_lane(void)
{
    putb(R(a, 0, WDATA) + 2, 1);
}
static void faults(void (*op)(void), const char *why)
{
    int status;
    pid_t p = fork();
    if (!p) {
        op();
        _exit(0);
    }
    check(p > 0 && waitpid(p, &status, 0) == p && WIFSIGNALED(status) && WTERMSIG(status) == SIGBUS,
          why);
}
int main(void)
{
    uint32_t map[] = {0x1f223000, 4096};
    unsigned t, n, t0, n0;
    int fd = open("/webos/dev/miomap", O_RDWR);
    check(fd >= 0 && !ioctl(fd, 0x40084901, map), "device/setup");
    a = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    b = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    check(a != MAP_FAILED && b != MAP_FAILED, "aliases");

    /* HAL_HWI2C_Master_Enable: reset pulse, then the enables */
    putb(R(a, 0, CTRL), 1);
    putb(R(a, 0, CTRL), 0x6c);
    check(getb(R(b, 0, CTRL)) == 0x6c && !flag(0) && (getb(R(b, 0, STATE)) & 31) == 0,
          "enabled, idle, no flag");
    putb(R(a, 0, RECV), 1);
    check(!flag(0), "a receive outside a read stays pending");

    /* the micom's power-on mode: write command 0x9A, repeated start, read */
    transfers(0, &t0, &n0);
    start(0);
    check(send(0, 0x52), "micom address acknowledged");
    check(send(0, 0x9a), "command byte acknowledged");
    start(0);
    check(send(0, 0x53), "micom read address acknowledged");
    check(recv(0, 0) == 0x20, "power-on mode: remote key (lgemu's answer)");
    check(recv(0, 1) == 0x00, "the rest of the answer");
    stop(0);
    transfers(0, &t, &n);
    check(t == t0 + 1 && n == n0, "one transfer: the write and the read together");

    /* a command the micom does not answer: its read is not acknowledged */
    start(0);
    check(send(0, 0x52) && send(0, 0x80), "RTC command written");
    start(0);
    check(!send(0, 0x53), "unanswered read: NACK");
    stop(0);

    /* no device: the address is not acknowledged */
    start(0);
    check(!send(0, 0x70), "unaddressed slave: NACK");
    check(!send(0, 0x00), "bytes after a NACKed address: NACK");
    stop(0);
    transfers(0, &t, &n);
    check(t == t0 + 2 && n == n0 + 1, "no transfer to an absent slave");

    /* the amplifier on port index 2: a write goes at its STOP; a read is refused */
    transfers(2, &t0, &n0);
    start(0x400);
    check(send(0x400, 0x54) && send(0x400, 0x01) && send(0x400, 0x02), "amplifier write");
    transfers(2, &t, &n);
    check(t == t0, "the write waits for its STOP");
    stop(0x400);
    transfers(2, &t, &n);
    check(t == t0 + 1 && n == n0, "written at STOP, acknowledged");
    start(0x400);
    check(!send(0x400, 0x55), "amplifier read: NACK");
    stop(0x400);
    start(0);
    check(!send(0, 0x54), "the amplifier is not on port index 0");
    stop(0);

    /* reset: idle, flag clear, a read in progress dropped */
    start(0);
    send(0, 0x52);
    send(0, 0x9a);
    start(0);
    send(0, 0x53);
    putb(R(a, 0, CTRL), 0x6d);
    check(!flag(0) && (getb(R(b, 0, STATE)) & 31) == 0, "reset idles the port");
    putb(R(a, 0, RECV), 1);
    check(!flag(0), "no read after a reset");
    putb(R(a, 0, CTRL), 0x6c);

    /* clock registers (HAL_HWI2C_SetClk, halfwords from byte 0x10): storage, through aliases */
    puth(R(a, 0, 0x12), 0x0055);
    check(geth(R(b, 0, 0x12)) == 0x0055, "clock register storage");
    faults(upper_lane, "upper half of a register slot rejected");
    puts("HWI2C master protocol: PASS");
    return 0;
}
