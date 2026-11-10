// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNC_TILEDIFF_H
#define VNC_TILEDIFF_H
/* Which TILEDIFF_TILE-square tiles of one composed frame differ from another's, on the GPU: one
 * fragment a tile draws into a small target, which is read back, one byte a tile. beanjar-vnc's
 * unchanged frames (docs/dev/architecture/vnc.md, "Unchanged frames"), for its GBM frames and for
 * the frames it composes into textures of its own, so that no frame is read back only for the
 * comparison. GLES 2. */
#define TILEDIFF_TILE 16
struct tilediff;
/* For pictures of width x height; the GL context must be current. */
struct tilediff *tilediff_new(int width, int height);
/* cur's tiles against prev's (textures of the picture's width, tex_h rows: the picture's height
 * or more, the picture in the first rows): drawn now, read with tilediff_read once the GL
 * commands so far are done. The grid's size in *tw, *th. Leaves the framebuffer binding and the
 * program as they were. */
void tilediff_run(struct tilediff *d, unsigned cur, unsigned prev, int tex_h, int *tw, int *th);
/* The last run's tiles, top row of tiles first, one byte each (nonzero: changed). Waits for the
 * GPU if it has not finished. */
void tilediff_read(struct tilediff *d, unsigned char *tiles);
void tilediff_free(struct tilediff *d);
#endif
