// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The media pool's host side (host/media/pool.c): the allocator and its limits, giving extents
 * back (punched out of the file: st_blocks), the directory as the guest sees it, and the rings
 * with hostile values from the guest. Prints TAP; exit status 0 only if every check passed. */
#define _GNU_SOURCE
#include "../../../host/media/pool.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define G WEBOS_MEDIA_POOL_GRANULE

static int n, failed;
static void ok(int cond, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("%s %d - ", cond ? "ok" : "not ok", ++n);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    failed += !cond;
}

static int new_pool(struct pool *p, uint32_t size)
{
    int fd = memfd_create("check-pool", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) || pool_init(p, fd, size, 0)) {
        perror("pool");
        exit(2);
    }
    return fd;
}

static void allocator(void)
{
    struct pool p;
    new_pool(&p, WEBOS_MEDIA_POOL_SIZE);
    ok(p.dir->magic == WEBOS_MEDIA_POOL_MAGIC && p.dir->version == WEBOS_MEDIA_POOL_VERSION &&
           p.dir->size == WEBOS_MEDIA_POOL_SIZE,
       "directory written");
    uint32_t off, size;
    int e = pool_alloc(&p, 0, 1, &off, &size);
    ok(e == 0 && off == G && size == G, "first extent after the directory, one granule");
    ok(p.dir->port[0].extent[0].state == WEBOS_MEDIA_EXTENT_LIVE &&
           p.dir->port[0].extent[0].offset == off && p.dir->port[0].extent[0].size == G,
       "LIVE in the directory");
    /* a 4K NV12 session: 4 slots of 3840x2160 NV12 */
    uint32_t nv12 = (3840 * 2160 * 3 / 2 + 4095) & ~4095u;
    e = pool_alloc(&p, 1, 4 * nv12, &off, &size);
    uint32_t size1 = size;
    ok(e == 0 && size >= 4 * nv12 && off == 2 * G, "a 4K NV12 session's slots fit (%u bytes)",
       size);
    int e2 = pool_alloc(&p, 2, 4 * nv12, &off, &size);
    ok(e2 == 0, "a second 4K session fits too");
    ok(pool_alloc(&p, 3, 4 * nv12, &off, &size) < 0, "a third does not: the pool is 128 MiB");
    ok(p.full == 1, "counted as full");
    for (int i = 1; i < WEBOS_MEDIA_POOL_EXTENTS; i++)
        pool_alloc(&p, 0, 1, &off, &size);
    ok(pool_alloc(&p, 0, 1, &off, &size) < 0, "a port has %d entries at most",
       WEBOS_MEDIA_POOL_EXTENTS);
    ok(pool_alloc(&p, WEBOS_MEDIA_PORTS, 1, &off, &size) < 0, "a port out of range refused");
    ok(pool_alloc(&p, 4, 0, &off, &size) < 0, "an empty extent refused");
    ok(pool_alloc(&p, 4, WEBOS_MEDIA_POOL_SIZE, &off, &size) < 0, "more than the pool refused");
    ok(pool_in_extent(&p, 1, 0, 2 * G, nv12) && !pool_in_extent(&p, 1, 0, 2 * G - 1, 2) &&
           !pool_in_extent(&p, 1, 0, 2 * G + size1, 1) && !pool_in_extent(&p, 2, 0, 2 * G, 1),
       "in_extent: inside, before, after, another port");
    ok(!pool_in_extent(&p, 1, 0, 0xffffffffu, 2) && !pool_in_extent(&p, 1, 7, 2 * G, 1) &&
           !pool_in_extent(&p, 1, -1, 2 * G, 1),
       "in_extent: wrapping, entries out of range");
    pool_fini(&p);
    close(p.fd);
}

static void retire_and_punch(void)
{
    struct pool p;
    new_pool(&p, WEBOS_MEDIA_POOL_SIZE);
    uint64_t idle = pool_file_bytes(&p);
    uint32_t off, size;
    int e = pool_alloc(&p, 3, 16u << 20, &off, &size);
    memset(p.base + off, 0x5a, size); /* the host writes pictures */
    uint64_t busy = pool_file_bytes(&p);
    ok(busy >= idle + size, "pages held while in use: %llu -> %llu KiB",
       (unsigned long long)idle >> 10, (unsigned long long)busy >> 10);
    /* the guest still maps it: an export counted */
    p.dir->port[3].exports[e] = 1;
    pool_retire(&p, 3, e);
    ok(p.dir->port[3].extent[e].state == WEBOS_MEDIA_EXTENT_RETIRING, "RETIRING in the directory");
    ok(pool_reap(&p) == 0 && pool_file_bytes(&p) == busy, "not freed while the guest maps it");
    ok(pool_alloc(&p, 4, WEBOS_MEDIA_POOL_SIZE - G - size + 1, &off, &size) < 0,
       "its granules not given to anyone else meanwhile");
    p.dir->port[3].exports[e] = 0;
    ok(pool_reap(&p) == 1, "reaped once the exports are 0");
    uint64_t after = pool_file_bytes(&p);
    ok(after <= idle, "punched: %llu KiB in the file after (%llu before the session)",
       (unsigned long long)after >> 10, (unsigned long long)idle >> 10);
    ok(p.dir->port[3].extent[e].state == WEBOS_MEDIA_EXTENT_FREE && pool_used_bytes(&p) == G,
       "FREE, and only the directory in use");
    ok(p.base[(size_t)G + 100] == 0, "a punched page reads as zero");
    pool_fini(&p);
    close(p.fd);
}

static void hostile_counts(void)
{
    struct pool p;
    new_pool(&p, 16 * G);
    uint32_t off, size, off2;
    int a = pool_alloc(&p, 0, G, &off, &size);
    int b = pool_alloc(&p, 1, G, &off2, &size);
    /* the guest writes zeros over every count and garbage over the directory's extents of port 1,
     * while nothing is retiring: the host frees nothing LIVE */
    memset(p.dir->port[0].exports, 0, sizeof p.dir->port[0].exports);
    p.dir->port[1].extent[b].state = WEBOS_MEDIA_EXTENT_RETIRING;
    p.dir->port[1].extent[b].offset = 0;
    p.dir->port[1].extent[b].size = 0xffff0000u;
    ok(pool_reap(&p) == 0 && pool_used_bytes(&p) == 3 * G,
       "a directory the guest scribbled on frees nothing LIVE");
    ok(pool_in_extent(&p, 1, b, off2, 1), "the host's own copy of the extents stands");
    /* a count that never drops: that VM's own space leaks, nothing else happens */
    p.dir->port[0].exports[a] = 0xffffffffu;
    pool_retire(&p, 0, a);
    ok(pool_reap(&p) == 0 && pool_used_bytes(&p) == 3 * G, "a stuck count only delays");
    p.dir->port[0].exports[a] = 0;
    ok(pool_reap(&p) == 1, "and frees once it drops");
    /* claim counters are only read */
    p.dir->port[5].claim = 0xdeadbeef;
    ok(pool_claim(&p, 5) == 0xdeadbeef && pool_claim(&p, 99) == 0, "claims read, out of range 0");
    pool_fini(&p);
    close(p.fd);
}

static void rings(void)
{
    static struct webos_media_control c;
    struct pool_rings r;
    rings_init(&r, &c);
    struct webos_media_picture pic = {.input = 1};
    int ring = -1, puts = 0;
    while (rings_put_picture(&r, &pic, &ring))
        puts++;
    ok(puts == WEBOS_MEDIA_RING_ENTRIES && ring == 0,
       "the picture ring holds %d, no ring while "
       "the guest is awake",
       puts);
    c.pictures.tail = 10;
    c.pictures.waiting = 1;
    ok(rings_put_picture(&r, &pic, &ring) == 1 && ring == 1 && c.pictures.head == 65,
       "room again after the guest took some; rung as it waits");
    /* hostile tails: ahead of the head, far behind: full, nothing written outside the ring */
    c.pictures.tail = r.pic_head + 5;
    ok(rings_put_picture(&r, &pic, &ring) == 0, "a tail ahead of the head counts as full");
    c.pictures.tail = r.pic_head - 1000;
    ok(rings_put_picture(&r, &pic, &ring) == 0, "a tail a ring behind counts as full");
    c.pictures.head = 0xdeadbeef; /* the guest overwrote the shared head: ours stands */
    c.pictures.tail = r.pic_head;
    ok(rings_put_picture(&r, &pic, &ring) == 1 && c.pictures.head == r.pic_head,
       "the host's own head is what it publishes");

    /* releases: none, some, a head far ahead (clamped to a ring), a budget */
    struct webos_media_release rel;
    unsigned budget = WEBOS_MEDIA_RING_ENTRIES;
    ok(!rings_take_release(&r, &rel, &budget), "no release");
    c.release[0] = (struct webos_media_release){G, 7};
    c.releases.head = 1;
    ok(rings_take_release(&r, &rel, &budget) && rel.slot == G && rel.input == 7 &&
           c.releases.tail == 1,
       "a release taken, the tail published");
    c.releases.head = 1 + 100000;
    budget = WEBOS_MEDIA_RING_ENTRIES;
    unsigned taken = 0;
    while (rings_take_release(&r, &rel, &budget))
        taken++;
    ok(taken == WEBOS_MEDIA_RING_ENTRIES && r.rel_tail == c.releases.head,
       "a head far ahead: one ring's worth taken (%u), then caught up", taken);
    c.releases.head = r.rel_tail - 5; /* behind the tail: wraps to a huge count, clamped */
    budget = 3;
    taken = 0;
    while (rings_take_release(&r, &rel, &budget))
        taken++;
    ok(taken == 3, "the budget bounds a pass (%u)", taken);
    /* the waiting flag */
    c.releases.head = r.rel_tail;
    ok(!rings_wait_release(&r) && c.releases.waiting == 1, "waiting set, nothing came");
    c.releases.head++;
    ok(rings_wait_release(&r), "one came between: no sleep");
    rings_done_waiting(&r);
    ok(c.releases.waiting == 0, "waiting cleared");
}

int main(void)
{
    allocator();
    retire_and_punch();
    hostile_counts();
    rings();
    printf("1..%d\n", n);
    return failed ? 1 : 0;
}
