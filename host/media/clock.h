// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef HOST_MEDIA_CLOCK_H
#define HOST_MEDIA_CLOCK_H
#include <stdint.h>
#include "../../include/webos-media.h"
#define OFFSET_BUCKET 2000000000ll /* minimum over two consecutive 2-second buckets */
struct offset {
    int64_t cur, prev, start;
};
static inline void offset_add(struct offset *o, int64_t sample, int64_t now)
{
    if (!o->start || now - o->start > OFFSET_BUCKET) {
        o->prev = o->start ? o->cur : INT64_MAX;
        o->cur = INT64_MAX;
        o->start = now;
    }
    if (sample < o->cur)
        o->cur = sample;
}

static inline int64_t offset_get(const struct offset *o)
{
    if (!o->start)
        return WEBOS_MEDIA_NONE;
    return o->cur < o->prev ? o->cur : o->prev;
}

#endif
