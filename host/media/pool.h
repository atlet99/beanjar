// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef HOST_MEDIA_POOL_H
#define HOST_MEDIA_POOL_H
/* The media pool's host side (include/webos-media.h, "The pool"): the allocator over the pool
 * file, its directory, giving extents back (punched out of the file), and the host's ends of a
 * session's rings. Everything read from the pool was written by the guest, or may have been:
 * these functions check it and never let it move them outside the pool. */
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "../../include/webos-media.h"

#define POOL_GRANULES (WEBOS_MEDIA_POOL_SIZE / WEBOS_MEDIA_POOL_GRANULE)

struct pool {
    int fd;
    uint8_t *base;
    uint32_t size;
    unsigned granules;
    uint64_t used[POOL_GRANULES / 64];
    struct webos_media_pool_dir *dir;
    /* the extents as the host granted them; the directory's copy is for the guest only */
    struct webos_media_extent ext[WEBOS_MEDIA_PORTS][WEBOS_MEDIA_POOL_EXTENTS];
    pthread_mutex_t lock;
    uint32_t generation;
    /* statistics */
    unsigned long long allocs, frees, punched, full;
};

/* Maps fd (size bytes, a multiple of the granule, at most WEBOS_MEDIA_POOL_SIZE) and writes the
 * directory. 0, or -1 with errno. */
int pool_init(struct pool *p, int fd, uint32_t size, uint32_t host_peer);
void pool_fini(struct pool *p);
/* An extent of at least bytes for port, LIVE in the directory: its entry, with *offset and *size
 * (granules); -1 when the port has no free entry or the pool no room. */
int pool_alloc(struct pool *p, unsigned port, uint32_t bytes, uint32_t *offset, uint32_t *size);
/* Entry e of port (or all of its entries, e < 0) RETIRING: no new exports. */
void pool_retire(struct pool *p, unsigned port, int e);
/* Frees every RETIRING extent whose exports the guest says are 0: punched out of the file, its
 * granules free, its entry FREE. The number freed. */
unsigned pool_reap(struct pool *p);
/* The guest's claim counter of port. */
uint32_t pool_claim(struct pool *p, unsigned port);
/* Whether [offset, offset + size) lies in a LIVE or RETIRING extent of port's entry e. */
int pool_in_extent(struct pool *p, unsigned port, int e, uint32_t offset, uint32_t size);
/* Bytes of granules in use, and bytes the file really holds (st_blocks). */
uint64_t pool_used_bytes(struct pool *p);
uint64_t pool_file_bytes(struct pool *p);

/* The host's ends of a session's rings: its own copies of the indices it owns (the shared ones
 * may be overwritten by the guest). */
struct pool_rings {
    struct webos_media_control *c;
    uint32_t pic_head; /* pictures written */
    uint32_t rel_tail; /* releases taken */
};
void rings_init(struct pool_rings *r, struct webos_media_control *c);
/* Puts p on the picture ring: 1, and *ring set when the guest sleeps (the caller rings it), or 0
 * when the ring is full (as the guest's tail says; a tail out of range counts as full). */
int rings_put_picture(struct pool_rings *r, const struct webos_media_picture *p, int *ring);
/* Takes the next release: 1, or 0 when there is none or *budget (decremented) is used up. A head
 * more than a ring ahead is clamped to the last ring's worth. */
int rings_take_release(struct pool_rings *r, struct webos_media_release *out, unsigned *budget);
/* Before sleeping until a release: the host's waiting flag set, then whether one came. */
int rings_wait_release(struct pool_rings *r);
void rings_done_waiting(struct pool_rings *r);
#endif
