// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The media pool's host side (pool.h). */
#define _GNU_SOURCE
#include "pool.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define G WEBOS_MEDIA_POOL_GRANULE

static int used(const struct pool *p, unsigned g)
{
    return (p->used[g / 64] >> (g % 64)) & 1;
}

static void mark(struct pool *p, unsigned first, unsigned n, int on)
{
    for (unsigned g = first; g < first + n; g++) {
        if (on)
            p->used[g / 64] |= 1ull << (g % 64);
        else
            p->used[g / 64] &= ~(1ull << (g % 64));
    }
}

int pool_init(struct pool *p, int fd, uint32_t size, uint32_t host_peer)
{
    memset(p, 0, sizeof *p);
    p->fd = -1;
    if (size < 2 * G || size % G || size > WEBOS_MEDIA_POOL_SIZE) {
        errno = EINVAL;
        return -1;
    }
    void *base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED)
        return -1;
    p->fd = fd;
    p->base = base;
    p->size = size;
    p->granules = size / G;
    p->dir = base;
    pthread_mutex_init(&p->lock, NULL);
    mark(p, 0, 1, 1); /* the directory */
    memset(p->dir, 0, sizeof *p->dir);
    p->dir->version = WEBOS_MEDIA_POOL_VERSION;
    p->dir->size = size;
    p->dir->host_peer = host_peer;
    __atomic_store_n(&p->dir->magic, WEBOS_MEDIA_POOL_MAGIC, __ATOMIC_RELEASE);
    return 0;
}

void pool_fini(struct pool *p)
{
    if (p->base)
        munmap(p->base, p->size);
    p->base = NULL;
    p->dir = NULL;
}

/* first fit */
static int find(struct pool *p, unsigned n)
{
    unsigned run = 0;
    for (unsigned g = 1; g < p->granules; g++) {
        run = used(p, g) ? 0 : run + 1;
        if (run == n)
            return (int)(g + 1 - n);
    }
    return -1;
}

int pool_alloc(struct pool *p, unsigned port, uint32_t bytes, uint32_t *offset, uint32_t *size)
{
    if (port >= WEBOS_MEDIA_PORTS || !bytes || bytes > p->size - G)
        return -1;
    unsigned n = (bytes + G - 1) / G;
    pthread_mutex_lock(&p->lock);
    struct webos_media_pool_port *pp = &p->dir->port[port];
    int e = -1;
    for (int i = 0; i < WEBOS_MEDIA_POOL_EXTENTS && e < 0; i++)
        if (p->ext[port][i].state == WEBOS_MEDIA_EXTENT_FREE)
            e = i;
    int first = e >= 0 ? find(p, n) : -1;
    if (first < 0) {
        p->full++;
        pthread_mutex_unlock(&p->lock);
        return -1;
    }
    mark(p, (unsigned)first, n, 1);
    struct webos_media_extent *x = &p->ext[port][e], *d = &pp->extent[e];
    x->offset = (uint32_t)first * G;
    x->size = n * G;
    x->generation = ++p->generation;
    x->state = WEBOS_MEDIA_EXTENT_LIVE;
    d->offset = x->offset;
    d->size = x->size;
    d->generation = x->generation;
    /* the entry is complete before the guest's kernel can see it LIVE */
    __atomic_store_n(&d->state, WEBOS_MEDIA_EXTENT_LIVE, __ATOMIC_RELEASE);
    p->allocs++;
    *offset = x->offset;
    if (size)
        *size = x->size;
    pthread_mutex_unlock(&p->lock);
    return e;
}

void pool_retire(struct pool *p, unsigned port, int e)
{
    if (port >= WEBOS_MEDIA_PORTS)
        return;
    pthread_mutex_lock(&p->lock);
    for (int i = 0; i < WEBOS_MEDIA_POOL_EXTENTS; i++) {
        struct webos_media_extent *x = &p->ext[port][i];
        if ((e < 0 || e == i) && x->state == WEBOS_MEDIA_EXTENT_LIVE) {
            x->state = WEBOS_MEDIA_EXTENT_RETIRING;
            __atomic_store_n(&p->dir->port[port].extent[i].state, WEBOS_MEDIA_EXTENT_RETIRING,
                             __ATOMIC_RELAXED);
        }
    }
    /* RETIRING is visible before reap reads the exports: the guest's kernel counts an export
     * first and then checks the state again (the same barrier on its side) */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&p->lock);
}

unsigned pool_reap(struct pool *p)
{
    unsigned freed = 0;
    pthread_mutex_lock(&p->lock);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (unsigned port = 0; port < WEBOS_MEDIA_PORTS; port++)
        for (int i = 0; i < WEBOS_MEDIA_POOL_EXTENTS; i++) {
            struct webos_media_pool_port *pp = &p->dir->port[port];
            struct webos_media_extent *x = &p->ext[port][i];
            /* the export count is the guest's: wrong, it only delays or leaks this extent */
            if (x->state != WEBOS_MEDIA_EXTENT_RETIRING ||
                __atomic_load_n(&pp->exports[i], __ATOMIC_ACQUIRE))
                continue;
            uint32_t off = x->offset, size = x->size;
            if (fallocate(p->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, off, size) == 0)
                p->punched += size;
            else
                memset(p->base + off, 0, size); /* not punched: at least nothing is left */
            mark(p, off / G, size / G, 0);
            memset(x, 0, sizeof *x);
            pp->extent[i].offset = pp->extent[i].size = 0;
            __atomic_store_n(&pp->extent[i].state, WEBOS_MEDIA_EXTENT_FREE, __ATOMIC_RELEASE);
            p->frees++;
            freed++;
        }
    pthread_mutex_unlock(&p->lock);
    return freed;
}

uint32_t pool_claim(struct pool *p, unsigned port)
{
    return port < WEBOS_MEDIA_PORTS ? __atomic_load_n(&p->dir->port[port].claim, __ATOMIC_ACQUIRE)
                                    : 0;
}

int pool_in_extent(struct pool *p, unsigned port, int e, uint32_t offset, uint32_t size)
{
    if (port >= WEBOS_MEDIA_PORTS || e < 0 || e >= WEBOS_MEDIA_POOL_EXTENTS)
        return 0;
    pthread_mutex_lock(&p->lock);
    const struct webos_media_extent *x = &p->ext[port][e];
    int in = (x->state == WEBOS_MEDIA_EXTENT_LIVE || x->state == WEBOS_MEDIA_EXTENT_RETIRING) &&
             offset >= x->offset && size <= x->size && offset - x->offset <= x->size - size;
    pthread_mutex_unlock(&p->lock);
    return in;
}

uint64_t pool_used_bytes(struct pool *p)
{
    uint64_t n = 0;
    pthread_mutex_lock(&p->lock);
    for (unsigned g = 0; g < p->granules; g++)
        n += used(p, g);
    pthread_mutex_unlock(&p->lock);
    return n * G;
}

uint64_t pool_file_bytes(struct pool *p)
{
    struct stat st;
    return fstat(p->fd, &st) ? 0 : (uint64_t)st.st_blocks * 512;
}

void rings_init(struct pool_rings *r, struct webos_media_control *c)
{
    r->c = c;
    r->pic_head = 0;
    r->rel_tail = 0;
    memset(c, 0, sizeof *c);
}

int rings_put_picture(struct pool_rings *r, const struct webos_media_picture *p, int *ring)
{
    struct webos_media_ring *pr = &r->c->pictures;
    uint32_t tail = __atomic_load_n(&pr->tail, __ATOMIC_ACQUIRE);
    /* the guest's tail is behind our head by at most a ring; anything else counts as full */
    if (r->pic_head - tail >= WEBOS_MEDIA_RING_ENTRIES)
        return 0;
    r->c->picture[r->pic_head % WEBOS_MEDIA_RING_ENTRIES] = *p;
    r->pic_head++;
    __atomic_store_n(&pr->head, r->pic_head, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *ring = __atomic_load_n(&pr->waiting, __ATOMIC_RELAXED) != 0;
    return 1;
}

int rings_take_release(struct pool_rings *r, struct webos_media_release *out, unsigned *budget)
{
    struct webos_media_ring *rr = &r->c->releases;
    if (!*budget)
        return 0;
    uint32_t head = __atomic_load_n(&rr->head, __ATOMIC_ACQUIRE);
    uint32_t avail = head - r->rel_tail;
    if (!avail)
        return 0;
    if (avail > WEBOS_MEDIA_RING_ENTRIES) /* a head out of range: the last ring's worth */
        r->rel_tail = head - WEBOS_MEDIA_RING_ENTRIES;
    struct webos_media_release *e = &r->c->release[r->rel_tail % WEBOS_MEDIA_RING_ENTRIES];
    out->slot = __atomic_load_n(&e->slot, __ATOMIC_RELAXED);
    out->input = __atomic_load_n(&e->input, __ATOMIC_RELAXED);
    r->rel_tail++;
    __atomic_store_n(&rr->tail, r->rel_tail, __ATOMIC_RELEASE);
    (*budget)--;
    return 1;
}

int rings_wait_release(struct pool_rings *r)
{
    struct webos_media_ring *rr = &r->c->releases;
    __atomic_store_n(&rr->waiting, 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return __atomic_load_n(&rr->head, __ATOMIC_ACQUIRE) != r->rel_tail;
}

void rings_done_waiting(struct pool_rings *r)
{
    __atomic_store_n(&r->c->releases.waiting, 0, __ATOMIC_RELAXED);
}
