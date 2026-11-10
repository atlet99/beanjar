// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* ARM32 O22N2 scaler protocol probe. ABI measured from stock libval-starfish. Run in an isolated VM
 * with videooutputd and avmutemanager stopped: this changes both scaler windows. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../../../include/webos-video.h"

#define S_EXT          0xc0185648UL
#define G_EXT          0xc0185647UL
#define S_CTRL         0xc008561cUL
#define G_CTRL         0xc008561bUL
#define SUBSCRIBE      0x4020565aUL
#define UNSUBSCRIBE    0x4020565bUL
#define DQEVENT        0x80805659UL
#define REGION         0x00982900U
#define CONNECT        0x0098290cU
#define ACTIVE         0x00982920U
#define APPLIED        0x00982922U
#define MUTE_OFF       0x0098291eU
#define BLANK          0x00980923U
#define VDO_CONNECT    0x00988e02U
#define VDO_DISCONNECT 0x00988e03U

static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s (errno %d)\n", __LINE__, #x, errno);                 \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static int ext(int fd, unsigned long cmd, uint32_t id, void *p, uint32_t size)
{
    uint32_t control[5] = {id, size, 0, (uint32_t)(uintptr_t)p, 0};
    uint32_t group[6] = {0x00980000, 1, 0, 0, 0, (uint32_t)(uintptr_t)control};
    return ioctl(fd, cmd, group);
}
static void subscription(int fd, unsigned long cmd, uint32_t id)
{
    uint32_t s[8] = {3, id};
    CHECK(ioctl(fd, cmd, s) == 0);
}
static void scalar(int fd, unsigned long cmd, uint32_t value)
{
    struct {
        uint32_t c[2], guard[2];
    } p = {{BLANK, value}, {0xface1234, 0x87654321}};
    CHECK(ioctl(fd, cmd, p.c) == 0);
    CHECK(p.guard[0] == 0xface1234 && p.guard[1] == 0x87654321);
    if (cmd == G_CTRL)
        CHECK(p.c[1] == value);
}
static uint32_t word(const unsigned char *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
static void dequeue(int fd, uint32_t id, unsigned sequence, const void *data, size_t size)
{
    unsigned char e[128];
    struct pollfd p = {fd, POLLPRI, 0};
    CHECK(poll(&p, 1, 1000) == 1 && (p.revents & POLLPRI));
    CHECK(ioctl(fd, DQEVENT, e) == 0);
    CHECK(word(e) == 3 && word(e + 88) == id);
    CHECK(word(e + 76) == sequence);
    CHECK(word(e + 80) > 0 && word(e + 84) < 1000000000);
    CHECK(memcmp(e + 8, data, size) == 0);
}
static void empty(int fd)
{
    unsigned char e[128];
    struct pollfd p = {fd, POLLPRI, 0};
    CHECK(poll(&p, 1, 0) == 0);
    CHECK(ioctl(fd, DQEVENT, e) == -1 && errno == EAGAIN);
}

static void connected(const char *name, int expected)
{
    struct webos_video_packet p, next;
    int fd = open("/dev/lgemu-video", O_RDONLY | O_NONBLOCK);
    CHECK(fd >= 0);
    ssize_t n = 0;
    unsigned read_count = 0;
    /* A new reader receives retained changes first; drain them to the actual
     * current table instead of treating an old event as the latest state. */
    for (unsigned i = 0; i < 32; i++) {
        n = read(fd, &next, sizeof(next));
        if (n != sizeof(next))
            break;
        p = next;
        read_count++;
    }
    CHECK(read_count && n == -1 && errno == EAGAIN);
    close(fd);
    CHECK(p.screen_w == 3840 && p.screen_h == 2160);
    for (unsigned i = 0; i < p.count; i++)
        if (!strcmp(p.contexts[i].context, name)) {
            CHECK(p.contexts[i].connected == (unsigned)expected);
            return;
        }
    CHECK(0);
}

int main(void)
{
    if (sizeof(void *) != 4) {
        fprintf(stderr, "Requires ARM32 userspace to exercise compat pointers.\n");
        return 77;
    }
    int main = open("/dev/video30", O_RDWR | O_NONBLOCK);
    int other = open("/dev/video30", O_RDWR | O_NONBLOCK);
    int sub = open("/dev/video31", O_RDWR | O_NONBLOCK);
    int vdo = open("/dev/video28", O_RDWR | O_NONBLOCK);
    int vdo_sub = open("/dev/video29", O_RDWR | O_NONBLOCK);
    CHECK(main >= 0 && other >= 0 && sub >= 0 && vdo >= 0 && vdo_sub >= 0);
    uint32_t route[2] = {0, 0xdeadbeef}, rb[2];
    CHECK(ext(vdo, S_EXT, VDO_DISCONNECT, route, sizeof(route)) == 0);
    CHECK(ext(vdo, G_EXT, VDO_CONNECT, rb, sizeof(rb)) == -1 && errno == ENODATA);
    empty(main);
    subscription(main, SUBSCRIBE, APPLIED);
    subscription(main, SUBSCRIBE, MUTE_OFF);
    subscription(other, SUBSCRIBE, APPLIED);
    subscription(sub, SUBSCRIBE, APPLIED);
    uint16_t r[14] = {0, 0, 1280, 720, 0, 0, 1280, 720, 640, 360, 1920, 1080, 0, 0};
    uint16_t back[14];
    CHECK(ext(main, S_EXT, REGION, r, sizeof(r)) == 0);
    CHECK(ext(other, G_EXT, REGION, back, sizeof(back)) == 0);
    CHECK(memcmp(r, back, sizeof(r)) == 0);
    unsigned char done[11] = {0};
    memcpy(done + 1, r + 8, 8);
    dequeue(main, APPLIED, 0, done, 11);
    dequeue(other, APPLIED, 0, done, 11);
    empty(sub);
    uint32_t c[3] = {4, 3, 1}, cb[3];
    CHECK(ext(main, S_EXT, CONNECT, c, sizeof(c)) == 0);
    CHECK(ext(other, G_EXT, CONNECT, cb, sizeof(cb)) == 0);
    CHECK(memcmp(c, cb, sizeof(c)) == 0);
    connected("MAIN", 0);
    route[1] = 3;
    CHECK(ext(vdo, S_EXT, VDO_CONNECT, route, sizeof(route)) == 0);
    CHECK(ext(vdo, G_EXT, VDO_CONNECT, rb, sizeof(rb)) == 0 && rb[0] == 0 && rb[1] == 3);
    connected("MAIN", 1);
    route[1] = 4;
    CHECK(ext(vdo, S_EXT, VDO_CONNECT, route, sizeof(route)) == 0);
    connected("MAIN", 0);
    route[1] = 3;
    CHECK(ext(vdo, S_EXT, VDO_CONNECT, route, sizeof(route)) == 0);
    connected("MAIN", 1);
    route[0] = 1;
    CHECK(ext(vdo, S_EXT, VDO_CONNECT, route, sizeof(route)) == -1 && errno == EINVAL);
    CHECK(ext(vdo_sub, S_EXT, VDO_CONNECT, route, sizeof(route)) == 0);
    connected("MAIN", 1);
    route[0] = 0;
    route[1] = 256;
    CHECK(ext(vdo, S_EXT, VDO_CONNECT, route, sizeof(route)) == -1 && errno == EOPNOTSUPP);
    CHECK(ext(vdo, S_EXT, VDO_CONNECT, route, 4) == -1 && errno == EINVAL);
    CHECK(ext(vdo, S_EXT, VDO_CONNECT, (void *)1, 8) == -1 && errno == EFAULT);
    CHECK(ext(vdo, S_EXT, REGION, route, 8) == -1 && errno == EOPNOTSUPP);
    connected("MAIN", 1);
    scalar(main, S_CTRL, 1);
    scalar(other, G_CTRL, 1);
    scalar(main, S_CTRL, 0);
    scalar(other, G_CTRL, 0);
    dequeue(main, MUTE_OFF, 1, done, 1);
    empty(main);
    empty(other);
    /* A second window and its subscribers must not affect MAIN. */
    r[8] = 160;
    CHECK(ext(sub, S_EXT, REGION, r, sizeof(r)) == 0);
    done[0] = 1;
    memcpy(done + 1, r + 8, 8);
    dequeue(sub, APPLIED, 0, done, 11);
    CHECK(ext(main, G_EXT, REGION, back, sizeof(back)) == 0 && back[8] == 640);
    empty(main);
    /* Active-region information is a black-bar detector result, not the crop. */
    CHECK(ext(main, G_EXT, 0x0098290d, back, 16) == -1 && errno == EOPNOTSUPP);
    uint32_t unsupported_event[8] = {3, ACTIVE};
    CHECK(ioctl(main, SUBSCRIBE, unsupported_event) == -1 && errno == EINVAL);
    unsupported_event[1] = APPLIED;
    unsupported_event[2] = 1;
    CHECK(ioctl(main, SUBSCRIBE, unsupported_event) == -1 && errno == EOPNOTSUPP);
    /* Rejected operations preserve existing state. */
    CHECK(ext(main, S_EXT, REGION, r, 27) == -1 && errno == EINVAL);
    CHECK(ext(main, S_EXT, REGION, (void *)1, 28) == -1 && errno == EFAULT);
    r[12] = 90;
    CHECK(ext(main, S_EXT, REGION, r, 28) == -1 && errno == EOPNOTSUPP);
    r[12] = 0;
    r[2] = 640;
    CHECK(ext(main, S_EXT, REGION, r, 28) == -1 && errno == EOPNOTSUPP);
    r[2] = 1280;
    r[0] = r[4] = 8;
    CHECK(ext(main, S_EXT, REGION, r, 28) == -1 && errno == EOPNOTSUPP);
    r[0] = r[4] = 0;
    c[0] = 2;
    CHECK(ext(main, S_EXT, CONNECT, c, 12) == -1 && errno == EOPNOTSUPP);
    c[0] = 4;
    c[2] = 2;
    CHECK(ext(main, S_EXT, CONNECT, c, 12) == -1 && errno == EOPNOTSUPP);
    c[2] = 1;
    c[1] |= 0x100;
    CHECK(ext(main, S_EXT, CONNECT, c, 12) == -1 && errno == EOPNOTSUPP);
    CHECK(ext(main, G_EXT, 0x0098290f, cb, 24) == -1 && errno == EOPNOTSUPP);
    CHECK(ioctl(main, 0x80685600UL, back) == -1 && errno == ENOTTY);
    uint32_t malformed[6] = {0x00980000, 2, 0, 0, 0, 1};
    CHECK(ioctl(main, S_EXT, malformed) == -1 && errno == EINVAL);
    malformed[1] = 1;
    CHECK(ioctl(main, S_EXT, malformed) == -1 && errno == EFAULT);
    CHECK(ext(main, G_EXT, REGION, back, sizeof(back)) == 0 && back[8] == 640);
    CHECK(ext(main, G_EXT, CONNECT, cb, sizeof(cb)) == 0 && cb[0] == 4 && cb[1] == 3 && cb[2] == 1);
    empty(main);
    subscription(other, UNSUBSCRIBE, APPLIED);
    r[2] = r[3] = 0;
    r[8] = r[9] = r[10] = r[11] = 0;
    CHECK(ext(main, S_EXT, REGION, r, sizeof(r)) == 0);
    CHECK(ext(main, G_EXT, REGION, back, sizeof(back)) == 0 && back[6] == 1280 && back[10] == 0);
    r[2] = 1280;
    r[3] = 720;
    r[8] = 800;
    r[9] = 360;
    r[10] = 1920;
    r[11] = 1080;
    CHECK(ext(main, S_EXT, REGION, r, sizeof(r)) == 0);
    empty(other);
    c[0] = c[1] = c[2] = 0;
    CHECK(ext(main, S_EXT, CONNECT, c, sizeof(c)) == 0);
    CHECK(ext(sub, S_EXT, CONNECT, c, sizeof(c)) == 0);
    route[0] = 0;
    route[1] = 0xdeadbeef;
    CHECK(ext(vdo, S_EXT, VDO_DISCONNECT, route, sizeof(route)) == 0);
    CHECK(ext(vdo, G_EXT, VDO_CONNECT, rb, sizeof(rb)) == -1 && errno == ENODATA);
    route[0] = 1;
    CHECK(ext(vdo_sub, S_EXT, VDO_DISCONNECT, route, sizeof(route)) == 0);
    connected("MAIN", 0);
    close(main);
    close(other);
    close(sub);
    close(vdo);
    close(vdo_sub);
    printf("PASS: %u scaler protocol checks\n", checks);
    return 0;
}
