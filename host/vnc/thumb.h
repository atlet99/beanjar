// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* beanjar-vnc's thumbnails: a frame it holds on the GPU drawn small with several samples a pixel,
 * read back as RGB (docs/dev/architecture/vnc.md, "Thumbnails"). With the GL context current. */
#ifndef BEANJAR_VNC_THUMB_H
#define BEANJAR_VNC_THUMB_H

struct thumb;

struct thumb *thumb_new(void);
/* The texture src, sw x sh, its top row first in memory, letterboxed into ow x oh (black around),
 * each output pixel the mean of 16 bilinear samples over its footprint: rgb gets ow * oh * 3
 * bytes, top row first. Leaves the framebuffer binding and the program as they were. 0 on
 * success. */
int thumb_draw(struct thumb *t, unsigned src, int sw, int sh, int ow, int oh, unsigned char *rgb);
void thumb_free(struct thumb *t);

#endif
