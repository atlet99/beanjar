// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* The media pool's access rules (grants.h). */
#include <linux/errno.h>
#include <linux/mm.h>
#include "grants.h"

int mp_grant_lookup(const struct webos_media_pool_dir *dir, u32 pool_size, u32 port, u32 offset,
                    u32 size, u32 *generation)
{
    u64 end = (u64)offset + size;
    int e;

    if (port >= WEBOS_MEDIA_PORTS)
        return -EINVAL;
    if (!size || !PAGE_ALIGNED(offset) || end > pool_size)
        return -EINVAL;
    for (e = 0; e < WEBOS_MEDIA_POOL_EXTENTS; e++) {
        const struct webos_media_extent *x = &dir->port[port].extent[e];
        u32 state = READ_ONCE(x->state);
        u32 start, len, gen;

        /* the fields after the state: the host publishes the state last */
        smp_rmb();
        start = READ_ONCE(x->offset);
        len = READ_ONCE(x->size);
        gen = READ_ONCE(x->generation);
        if (state != WEBOS_MEDIA_EXTENT_LIVE)
            continue;
        /* never the directory, never beyond the pool, whatever the host wrote */
        if (start < WEBOS_MEDIA_POOL_GRANULE || (u64)start + len > pool_size)
            continue;
        if (offset >= start && end <= (u64)start + len) {
            *generation = gen;
            return e;
        }
    }
    return -EPERM;
}

bool mp_extent_live(const struct webos_media_pool_dir *dir, u32 port, int e, u32 generation)
{
    const struct webos_media_extent *x = &dir->port[port].extent[e];

    return READ_ONCE(x->state) == WEBOS_MEDIA_EXTENT_LIVE && READ_ONCE(x->generation) == generation;
}

int mp_claim(const void **owner, u32 *claims, const void *file, int *cur, u32 port)
{
    if (port >= WEBOS_MEDIA_PORTS)
        return -EINVAL;
    if (owner[port] && owner[port] != file)
        return -EBUSY;
    if (*cur == (int)port)
        return 0;
    mp_unclaim(owner, claims, file, cur);
    owner[port] = file;
    claims[port]++;
    *cur = port;
    return 0;
}

int mp_unclaim(const void **owner, u32 *claims, const void *file, int *cur)
{
    int port = *cur;

    if (port < 0 || owner[port] != file)
        return -1;
    owner[port] = NULL;
    claims[port]++;
    *cur = -1;
    return port;
}
