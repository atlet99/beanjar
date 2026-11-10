/* SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors */
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The media pool's access rules (grants.c), apart from the device so that KUnit can test them
 * (tests/media-pool-kunit.c).
 */
#ifndef MEDIA_POOL_GRANTS_H
#define MEDIA_POOL_GRANTS_H

#include <linux/types.h>
#include "../../../../include/webos-media.h"

/*
 * The LIVE extent of port that holds [offset, offset + size) in a pool of pool_size bytes, as the
 * directory says: its index (*generation its generation), -EINVAL for a range that is not page
 * aligned or empty, -EPERM when no extent of the port holds it. Each extent is read once, so a
 * host writing the directory meanwhile cannot make a range pass that the snapshot refuses.
 */
int mp_grant_lookup(const struct webos_media_pool_dir *dir, u32 pool_size, u32 port, u32 offset,
                    u32 size, u32 *generation);
/* Whether extent e of port is still LIVE with generation. */
bool mp_extent_live(const struct webos_media_pool_dir *dir, u32 port, int e, u32 generation);

/*
 * Claims: owner[port] is the file that has the port, *cur the port the file has (-1: none),
 * claims[port] counts claims and their ends (odd while claimed). 0, -EINVAL for a port out of
 * range, -EBUSY when another file has it. A file that claims another port gives up its first.
 */
int mp_claim(const void **owner, u32 *claims, const void *file, int *cur, u32 port);
/* The file's claim ended (closed): its port free. The port, or -1 when it had none. */
int mp_unclaim(const void **owner, u32 *claims, const void *file, int *cur);

#endif
