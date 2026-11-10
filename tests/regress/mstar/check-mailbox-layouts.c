// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* AArch32 VM check. Packed layouts: Utopia 700 linux/mdrv_mbx_st.h;
 * aligned layouts: the older LG W18R driver. Use a private test class, never
 * the native PM/IR or TEE classes. No real device identifiers are involved. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int fd = -1;
static void check(int good, const char *what)
{
    if (good)
        return;
    fprintf(stderr, "FAIL %s\n", what);
    if (fd >= 0)
        close(fd);
    exit(1);
}
static void put32(uint8_t *p, uint32_t n)
{
    memcpy(p, &n, 4);
}
static uint32_t get32(const uint8_t *p)
{
    uint32_t n;
    memcpy(&n, p, 4);
    return n;
}
static void request(unsigned nr, uint8_t *buf, unsigned n, int result)
{
    memset(buf + n, 0xa5, 4);
    check(ioctl(fd, _IOC(_IOC_READ | _IOC_WRITE, 'm', nr, n), buf) == 0, "ioctl accepted");
    check(get32(buf + n) == UINT32_C(0xa5a5a5a5), "output stays within wire struct");
    if (result)
        check(get32(buf + n - 4) == 0, "native result offset");
}
static void run(int packed)
{
    uint8_t a[32] = {0}, msg[24] = {0}, out[24] = {0};
    unsigned bsize = packed ? 5 : 8, rsize = packed ? 10 : 12;
    fd = open("/dev/msmailbox", O_RDWR);
    check(fd >= 0, "open mailbox");
    put32(a, 2);
    put32(a + 4, 0);
    put32(a + 8, 300);
    request(0, a, 16, 1);
    memset(a, 0, sizeof(a));
    a[0] = 1;
    put32(a + bsize - 4, UINT32_C(0xdeadbeef));
    request(9, a, bsize, 1);
    memset(a, 0, sizeof(a));
    put32(a, 220);
    a[4] = 4;
    put32(a + rsize - 4, UINT32_C(0xdeadbeef));
    request(2, a, rsize, 1);
    memset(a, 0, sizeof(a));
    request(12, a, bsize, 0);
    check(a[0] == 1 && get32(a + bsize - 4) >= 1, "enabled and reference count");

    /* Exercise the packed byte between the pointer and result as well. FRC
     * is unused here; preserve its original information for the next client. */
    uint8_t saved[8], sample[8] = {0x5a, 0xa5, 1, 2, 3, 4, 5, 6}, got[8];
    unsigned isize = packed ? 13 : 16;
    memset(a, 0, sizeof(a));
    put32(a, 4);
    put32(a + 4, (uintptr_t)saved);
    a[8] = 8;
    request(11, a, isize, 1);
    put32(a + 4, (uintptr_t)sample);
    request(10, a, isize, 1);
    put32(a + 4, (uintptr_t)got);
    request(11, a, isize, 1);
    put32(a + 4, (uintptr_t)saved);
    request(10, a, isize, 1);
    check(!memcmp(got, sample, sizeof(got)), "information round trip");

    /* Loopback validates delivery, then CHECK/REMOVE's packed boolean/result. */
    msg[9] = 220;
    msg[10] = 8;
    msg[11] = 1;
    msg[12] = 0x5a;
    memset(a, 0, sizeof(a));
    put32(a, (uintptr_t)msg);
    request(6, a, 8, 1);
    memset(a, 0, sizeof(a));
    put32(a, 220);
    put32(a + 4, (uintptr_t)out);
    put32(a + 12, 4);
    request(13, a, 20, 1);
    check(out[9] == 220 && out[10] == 8 && out[11] == 1 && out[12] == 0x5a,
          "checked loopback payload");
    memset(a, 0, sizeof(a));
    request(14, a, bsize, 1);

    memset(a, 0, sizeof(a));
    put32(a, 220);
    a[4] = 1;
    request(3, a, packed ? 9 : 12, 1);
    memset(a, 0, sizeof(a));
    a[0] = 1;
    request(1, a, bsize, 1);
    close(fd);
    fd = -1;
}
int main(void)
{
    check(sizeof(void *) == 4, "AArch32 required");
    run(0);
    run(1);
    puts("PASS aligned and packed mailbox layouts: enable, information, queues, loopback, cleanup");
    return 0;
}
