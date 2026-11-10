// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNC_FRAMES_H
#define VNC_FRAMES_H
/* beanjar-vnc's GBM frames (docs/dev/architecture/vnc.md, "Frames on RK3399"): the picture composed
 * by the GPU straight into linear XRGB8888 buffer objects on the render node, each a colour
 * attachment (EGL image) and a dma-buf, handed to neatvnc as GBM frames (its Open H.264 encoder
 * takes them uncopied) and to the video socket's encoders, with no read back on the CPU. Each
 * buffer has its height rounded up to a multiple of 16 rows (V4L2 encoders read whole macroblock
 * rows); neatvnc gets a view of it at the picture's height. */
#include "frame.h"

struct gbm_bo;
struct frame {
    struct vnc_frame v;
    struct gbm_bo *bo;   /* the allocation, height rounded up */
    struct gbm_bo *view; /* the same memory at the picture's height (or bo) */
    void *image;         /* EGLImageKHR */
    unsigned tex, fbo;
};
struct frames;
/* A pool of count buffers of width x height on the first render node whose buffers the current
 * EGL context renders into; NULL with the reason printed when there is none (no GBM, no dma-buf
 * import, an incomplete framebuffer). The GL context must be current. */
struct frames *frames_new(int width, int height, int count);
/* A buffer nobody holds (one reference, the caller's), or NULL: all are in use. Main thread. */
struct frame *frames_acquire(struct frames *p);
/* The render node it runs on, for the log. */
const char *frames_node(const struct frames *p);
/* Which tiles of cur differ from prev (FRAMES_TILE pixels square, top row of tiles first): drawn by
 * the GPU into a small target now, read with frames_diff_read once the GL commands so far are done
 * (after the frame's fence: no wait). The tile grid's size in *tw, *th. */
#define FRAMES_TILE 16
void frames_diff(struct frames *p, struct frame *cur, struct frame *prev, int *tw, int *th);
/* The last frames_diff's tiles, one byte each (nonzero: changed), tw * th of them. */
void frames_diff_read(struct frames *p, unsigned char *tiles);
/* Frees the pool; every frame must have been let go. GL context current. */
void frames_free(struct frames *p);
#endif
