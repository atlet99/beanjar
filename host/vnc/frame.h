// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNC_FRAME_H
#define VNC_FRAME_H
/* A composed picture in a GBM buffer object (frames.c), as the encoders see it: a dma-buf they
 * import (the V4L2 encoders, the converter) or read through its read-only mapping (the CPU
 * encoders). Reference-counted from any thread; at the last reference it goes back to its pool
 * (release, which only marks it free). Its pixels are complete when it is handed over (the
 * render's fence has signalled), and stay unchanged while a reference is held.
 * docs/dev/architecture/vnc.md, "Frames on RK3399". */
#include <glib.h>
#include <stddef.h>
#include <stdint.h>

struct vnc_frame {
    int fd;                      /* the dma-buf (owned by the pool) */
    uint32_t fourcc;             /* DRM_FORMAT_XRGB8888: bytes B, G, R, X */
    int width, height;           /* the picture's; the buffer has rows up to a multiple of 16 */
    unsigned stride;             /* bytes a row */
    size_t size;                 /* the dma-buf's bytes: at least stride * the padded height */
    const unsigned char *pixels; /* the dma-buf mapped read-only (write-combined: read once) */
    gint refs;
    void (*release)(struct vnc_frame *);
};

static inline void vnc_frame_ref(struct vnc_frame *f)
{
    g_atomic_int_inc(&f->refs);
}
static inline void vnc_frame_unref(struct vnc_frame *f)
{
    if (f && g_atomic_int_dec_and_test(&f->refs) && f->release)
        f->release(f);
}
#endif
