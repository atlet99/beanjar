// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/*
 * The media pool device's ABI (media-pool.ko, include/webos-media.h "The pool"), kselftest style:
 * run in the guest as a 32-bit webOS process, TAP on stdout, exit status 0 only if every check
 * passed. The host's media helper is the other side: the test takes a free media port, claims
 * it, and asks for a DECODE session with shared pictures, so the host grants a control extent
 * (POOL) and, for a tiny JPEG, a picture extent; the checks then run against those grants.
 *
 *   media-pool-selftest [LOOPS]
 *
 * CLAIM: exclusivity (EBUSY), a port out of range, moving to another port, the claim's end on
 * close; EXPORT: refused without a claim, inside the grants only (the directory, another port's
 * range, beyond the pool, unaligned, empty), the dma-buf mapped, written and read, closed; RING
 * only with a claim; poll and read follow the doorbell (the host rings for a picture while the
 * guest waits); a retired extent refuses new exports while an old mapping stays good, and comes
 * back for the next session once unmapped; open/close and claim loops; two processes contending.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../../../../../include/webos-media.h"
#include "tiny-jpeg.h"

static int n, failed;

static void ok(int cond, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    printf("%s %d - ", cond ? "ok" : "not ok", ++n);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fflush(stdout);
    failed += !cond;
}

static int dev(void)
{
    return open(WEBOS_MEDIA_POOL_DEVICE, O_RDWR | O_NONBLOCK | O_CLOEXEC);
}

static int claim(int fd, unsigned int port)
{
    return ioctl(fd, WEBOS_MEDIA_IOC_CLAIM, &port) ? -errno : 0;
}

static int export(int fd, unsigned int offset, unsigned int size, int *out)
{
    struct webos_media_export e = {.offset = offset, .size = size, .fd = -1};

    if (ioctl(fd, WEBOS_MEDIA_IOC_EXPORT, &e))
        return -errno;
    *out = e.fd;
    return 0;
}

/* ---- the host side, over a media port ---- */

static int port_fd = -1, port_index = -1;
static unsigned int sequence;

static long long now_ns(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000ll + t.tv_nsec;
}

static int send_msg(unsigned short type, const void *a, size_t alen, const void *b, size_t blen)
{
    struct webos_media_header h = {
        .magic = WEBOS_MEDIA_MAGIC,
        .type = type,
        .size = alen + blen,
        .sequence = ++sequence,
        .mono = now_ns(),
        .clock = WEBOS_MEDIA_NONE,
    };
    char buf[sizeof(h) + 4096];

    h.check = webos_media_check(&h);
    if (sizeof(h) + alen + blen > sizeof(buf))
        return -1;
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), a, alen);
    if (blen)
        memcpy(buf + sizeof(h) + alen, b, blen);
    size_t len = sizeof(h) + alen + blen, done = 0;

    while (done < len) {
        ssize_t w = write(port_fd, buf + done, len - done);

        if (w > 0) {
            done += w;
            continue;
        }
        if (w < 0 && errno != EAGAIN)
            return -1;
        poll(&(struct pollfd){.fd = port_fd, .events = POLLOUT}, 1, 100);
    }
    return h.sequence;
}

/* Reads the port until a message of type comes (its payload in out, at most max): 0, or -1. */
static int recv_msg(unsigned short type, void *out, size_t max, int timeout_ms)
{
    static char buf[1 << 16];
    static size_t have;
    long long end = now_ns() + timeout_ms * 1000000ll;

    for (;;) {
        struct webos_media_header h;

        while (have >= sizeof(h)) {
            memcpy(&h, buf, sizeof(h));
            if (h.magic != WEBOS_MEDIA_MAGIC || h.check != webos_media_check(&h)) {
                memmove(buf, buf + 1, --have);
                continue;
            }
            if (have < sizeof(h) + h.size)
                break;
            int mine = h.type == type;

            if (mine)
                memcpy(out, buf + sizeof(h), h.size < max ? h.size : max);
            have -= sizeof(h) + h.size;
            memmove(buf, buf + sizeof(h) + h.size, have);
            if (mine)
                return 0;
        }
        long long left = end - now_ns();

        if (left <= 0)
            return -1;
        poll(&(struct pollfd){.fd = port_fd, .events = POLLIN}, 1, left / 1000000 + 1);
        ssize_t r = read(port_fd, buf + have, sizeof(buf) - have);

        if (r > 0)
            have += r;
    }
}

static int open_port(void)
{
    for (int i = WEBOS_MEDIA_PORTS - 1; i >= 0; i--) {
        char path[64];

        snprintf(path, sizeof(path), "/dev/lg/" WEBOS_MEDIA_PORT_NAME "%d", i);
        port_fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (port_fd >= 0) {
            port_index = i;
            return 0;
        }
    }
    return -1;
}

/* OPEN and DECODE (shared): the host's POOL answer. */
static int begin_session(struct webos_media_pool *pool)
{
    struct webos_media_open o = {.kind = WEBOS_MEDIA_VIDEO, .pid = getpid(), .channel = -1};
    struct webos_media_decode d = {.format = WEBOS_MEDIA_NV12, .flags = WEBOS_MEDIA_DECODE_SHARED};
    const char caps[] = "image/jpeg, width=(int)32, height=(int)32";

    snprintf(o.element, sizeof(o.element), "selftest");
    if (send_msg(WEBOS_MEDIA_OPEN, &o, sizeof(o), NULL, 0) < 0 ||
        send_msg(WEBOS_MEDIA_DECODE, &d, sizeof(d), NULL, 0) < 0 ||
        send_msg(WEBOS_MEDIA_CAPS, caps, sizeof(caps), NULL, 0) < 0)
        return -1;
    return recv_msg(WEBOS_MEDIA_POOL, pool, sizeof(*pool), 5000);
}

int main(int argc, char **argv)
{
    int loops = argc > 1 ? atoi(argv[1]) : 200;
    long page = sysconf(_SC_PAGESIZE);
    int a, b, buf;

    a = dev();
    if (a < 0) {
        printf("1..0 # SKIP no %s: %s\n", WEBOS_MEDIA_POOL_DEVICE, strerror(errno));
        return 4; /* kselftest's skip */
    }
    if (open_port()) {
        printf("1..0 # SKIP no free media port\n");
        return 4;
    }
    printf("# media port %d\n", port_index);

    /* ---- refusals before anything is claimed or granted ---- */
    ok(export(a, WEBOS_MEDIA_POOL_GRANULE, page, &buf) == -EPERM, "EXPORT without a claim: EPERM");
    ok(ioctl(a, WEBOS_MEDIA_IOC_RING) < 0 && errno == EPERM, "RING without a claim: EPERM");
    ok(claim(a, WEBOS_MEDIA_PORTS) == -EINVAL, "CLAIM of a port out of range: EINVAL");
    ok(ioctl(a, _IO('W', 99)) < 0 && errno == ENOTTY, "an unknown ioctl: ENOTTY");
    {
        unsigned int count = 0;

        ok(read(a, &count, 4) == 4, "read gives the interrupt count (%u) and never blocks", count);
        ok(read(a, &count, 2) < 0 && errno == EINVAL, "read of less than 4 bytes: EINVAL");
        ok(poll(&(struct pollfd){.fd = a, .events = POLLIN}, 1, 100) == 0,
           "poll: nothing since the last read");
    }

    /* ---- claims ---- */
    b = dev();
    ok(claim(a, port_index) == 0, "CLAIM port %d", port_index);
    ok(claim(a, port_index) == 0, "CLAIM again by the same file");
    ok(claim(b, port_index) == -EBUSY, "another file: EBUSY");
    int other = (port_index + 1) % WEBOS_MEDIA_PORTS;

    ok(claim(b, other) == 0, "another file takes another port");
    close(b);
    b = dev();
    ok(claim(b, other) == 0, "a port is free once its file closed");
    close(b);
    ok(ioctl(a, WEBOS_MEDIA_IOC_RING) == 0, "RING with a claim");

    /* ---- the host's grants ---- */
    struct webos_media_pool pool = {0};

    ok(begin_session(&pool) == 0 && pool.size >= sizeof(struct webos_media_control),
       "the host grants a control extent: %u bytes at %#x", pool.size, pool.offset);
    if (!pool.size) {
        printf("1..%d\n", n);
        return 1;
    }
    struct webos_media_control *c = NULL;
    int cfd = -1;

    ok(export(a, pool.offset, pool.size, &cfd) == 0 && cfd >= 0, "EXPORT the control extent");
    c = mmap(NULL, pool.size, PROT_READ | PROT_WRITE, MAP_SHARED, cfd, 0);
    ok(c != MAP_FAILED, "mmap the dma-buf");
    ok(c != MAP_FAILED && c->pictures.head == 0 && c->releases.head == 0, "a fresh control");
    ok(mmap(NULL, pool.size + page, PROT_READ, MAP_SHARED, cfd, 0) == MAP_FAILED,
       "mmap beyond the dma-buf refused");
    ok(export(a, pool.offset, pool.size + page, &buf) == -EPERM, "EXPORT past the extent: EPERM");
    ok(export(a, pool.offset - page, page, &buf) == -EPERM, "EXPORT before it: EPERM");
    ok(export(a, 0, page, &buf) == -EPERM, "EXPORT of the directory: EPERM");
    ok(export(a, pool.offset + 1, page, &buf) == -EINVAL, "EXPORT unaligned: EINVAL");
    ok(export(a, pool.offset, 0, &buf) == -EINVAL, "EXPORT of 0 bytes: EINVAL");
    ok(export(a, WEBOS_MEDIA_POOL_SIZE - page, 2 * page, &buf) == -EINVAL,
       "EXPORT beyond the pool: EINVAL");
    ok(export(a, 0xfffff000u, 0x2000, &buf) == -EINVAL, "EXPORT wrapping: EINVAL");
    b = dev();
    claim(b, other);
    ok(export(b, pool.offset, page, &buf) == -EPERM, "another port's file: EPERM");
    close(b);

    /* ---- a picture: the host rings while the guest waits ---- */
    unsigned int count;

    read(a, &count, 4);
    __atomic_store_n(&c->pictures.waiting, 1, __ATOMIC_SEQ_CST);
    struct webos_media_buffer bb = {
        .pts = 0, .dts = WEBOS_MEDIA_NONE, .duration = 1, .flags = WEBOS_MEDIA_KEYFRAME};
    int input = send_msg(WEBOS_MEDIA_BUFFER, &bb, sizeof(bb), tiny_jpeg, sizeof(tiny_jpeg));
    struct pollfd pf = {.fd = a, .events = POLLIN};
    int woke = poll(&pf, 1, 5000) == 1;

    ok(woke, "poll wakes for the host's ring");
    __atomic_store_n(&c->pictures.waiting, 0, __ATOMIC_SEQ_CST);
    struct webos_media_picture pic = {0};
    long long end = now_ns() + 5000000000ll;
    unsigned int tail = 0, last = 0;

    while (!last && now_ns() < end) {
        while (tail != __atomic_load_n(&c->pictures.head, __ATOMIC_ACQUIRE)) {
            struct webos_media_picture *p = &c->picture[tail++ % WEBOS_MEDIA_RING_ENTRIES];

            if (p->format)
                pic = *p;
            last |= p->input == (unsigned int)input && (p->flags & WEBOS_MEDIA_PICTURE_LAST);
        }
        __atomic_store_n(&c->pictures.tail, tail, __ATOMIC_RELEASE);
        usleep(10000);
    }
    ok(last && pic.format == WEBOS_MEDIA_NV12 && (pic.flags & WEBOS_MEDIA_PICTURE_SHARED),
       "a shared picture and the LAST answer on the ring (slot %#x)", pic.slot);
    int pfd = -1;
    unsigned char *px = MAP_FAILED;

    if (pic.format) {
        ok(export(a, pic.slot, 32 * 32 * 3 / 2, &pfd) == 0, "EXPORT the picture's slot");
        px = mmap(NULL, page, PROT_READ, MAP_SHARED, pfd, 0);
        ok(px != MAP_FAILED && px[0] != 0, "its pixels are there (Y %u)",
           px == MAP_FAILED ? 0 : px[0]);
    }

    /* ---- the end of the session: new exports refused, old mappings good ---- */
    send_msg(WEBOS_MEDIA_CLOSE, NULL, 0, NULL, 0);
    usleep(300000);
    ok(export(a, pool.offset, page, &buf) == -EPERM, "a retired extent: no new EXPORT");
    /* past the rings, where the host never writes */
    volatile unsigned int *mark = (unsigned int *)((char *)c + pool.size) - 1;

    *mark = 0x5a5a5a5a;
    ok(*mark == 0x5a5a5a5a, "the old mapping still works (the host keeps it while mapped)");
    if (px != MAP_FAILED)
        munmap(px, page);
    if (pfd >= 0)
        close(pfd);
    munmap(c, pool.size);
    close(cfd);
    usleep(300000);
    struct webos_media_pool again = {0};

    ok(begin_session(&again) == 0 && again.size && again.offset <= pool.offset,
       "unmapped, it went back: the next session's control at %#x (was %#x)", again.offset,
       pool.offset);
    if (again.offset == pool.offset) {
        /* the host punched the old extent out of its file: what the guest wrote is gone */
        int afd = -1;
        struct webos_media_control *ac = MAP_FAILED;

        if (export(a, again.offset, again.size, &afd) == 0)
            ac = mmap(NULL, again.size, PROT_READ, MAP_SHARED, afd, 0);
        unsigned int v = ac == MAP_FAILED
                             ? 0xffffffffu
                             : ((volatile unsigned int *)((char *)ac + again.size))[-1];

        ok(v == 0, "the same range again reads zero where the last session wrote (%#x)", v);
        if (ac != MAP_FAILED)
            munmap(ac, again.size);
        if (afd >= 0)
            close(afd);
    }
    send_msg(WEBOS_MEDIA_CLOSE, NULL, 0, NULL, 0);

    /* ---- loops ---- */
    int bad = 0;

    for (int i = 0; i < loops; i++) {
        int f = dev();

        bad += f < 0 || claim(f, other) != 0;
        if (f >= 0)
            close(f);
    }
    ok(!bad, "%d open, claim, close loops (%d failed)", loops, bad);
    bad = 0;
    for (int i = 0; i < loops; i++)
        bad += claim(a, i % 2 ? port_index : other) != 0;
    ok(!bad, "%d claims moving between two ports (%d failed)", loops, bad);
    claim(a, port_index);

    /* ---- two processes contending for one port ---- */
    close(a);
    int wins = 0, rounds = loops / 4 + 1;

    for (int i = 0; i < rounds; i++) {
        int pipefd[2];

        pipe(pipefd);
        pid_t child = fork();

        if (!child) {
            int f = dev();
            char go;

            read(pipefd[0], &go, 1);
            int r = claim(f, other);

            usleep(2000);
            _exit(r == 0 ? 1 : r == -EBUSY ? 0 : 2);
        }
        int f = dev();

        write(pipefd[1], "x", 1);
        int mine = claim(f, other) == 0;

        usleep(2000);
        close(f);
        int st;

        waitpid(child, &st, 0);
        int theirs = WEXITSTATUS(st);

        wins += mine + (theirs == 1);
        bad += theirs == 2 || mine + (theirs == 1) == 0;
        close(pipefd[0]);
        close(pipefd[1]);
    }
    ok(!bad && wins >= rounds && wins <= 2 * rounds,
       "%d rounds of two processes claiming one port: never both refused, %d claims won", rounds,
       wins);
    close(port_fd);
    printf("1..%d\n", n);
    return failed ? 1 : 0;
}
