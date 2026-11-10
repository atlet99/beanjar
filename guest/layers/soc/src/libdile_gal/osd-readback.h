// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libdile_gal: pictures read back from the GPU (osd-readback.c), for blits that read the OSD
 * plane's frame buffer or the compositor's windows (dile-gal.c). */
#ifndef OSD_READBACK_H
#define OSD_READBACK_H
#include <stdint.h>
/* A DRM framebuffer (fb_id 0: the one on screen) scaled (area average) to dw x dh pixels of
 * B,G,R,A bytes (premultiplied, alpha 0 where video shows through), tightly packed at dst. 0, or
 * -1 if there is no picture to read. */
int osd_readback_argb(uint32_t fb_id, uint8_t *dst, int dw, int dh);
#endif
