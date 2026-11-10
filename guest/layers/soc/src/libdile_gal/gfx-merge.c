// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libdile_gal: the compositor's place in gm's merges, in gm.
 *
 * gm shows one window on frame buffer controller 0 directly, or, when several are visible, merges
 * them by Z-order into the back buffer of its MERGE window and flips that onto the controller:
 * it clears the buffer, then blits each visible window's front buffer into it, the lowest by copy
 * and the rest by source-over (premultiplied, DILE_GAL_Blit flags 0xe3, blend ONE/INVSRCALPHA).
 * The compositor's WEBOS window sits at Z-order 16: tvwin (17), the boot animation (18) and the
 * cursor (19) are above it, gm's subtitles, teletext, MHEG, MHP, HbbTV and captions below.
 *
 * On the TV the compositor has rendered into that window's buffers. Here its picture is in the
 * GPU (egl-kms), so the blit that would read the window leaves a placeholder instead: what the
 * merge holds so far (the windows below) moves to a second surface of the same size, "under",
 * the merge buffer is cleared for the windows above, and the blit's rectangles are kept. When gm
 * sets that buffer on a controller, the controller's state says where the compositor goes
 * (gfx_merge_lookup), and egl-kms scans out under, the compositor, then the buffer. Clearing or
 * replacing the whole buffer starts a new merge and forgets the placeholder. */
#include "gal.h"
#include <pthread.h>
#include <string.h>

#define MERGES 4
#define PAGE   4096u

static struct merge {
    uint32_t dst, size; /* the merge buffer */
    uint32_t under;     /* the buffer's own "under" surface (0: none yet) */
    int under_valid;
    int slot; /* the compositor window merged, -1 for none */
    rect_t src, dstr;
    uint32_t alpha;
} merges[MERGES];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static struct merge *find(uint32_t offset)
{
    for (int i = 0; i < MERGES; i++)
        if (merges[i].dst == offset)
            return &merges[i];
    return NULL;
}

void gfx_merge_write(const surface_t *dst, int how)
{
    struct merge *m;
    if (!how)
        return;
    pthread_mutex_lock(&mutex);
    m = find(dst->offset);
    if (m) {
        m->slot = -1;
        m->under_valid = 0;
    }
    pthread_mutex_unlock(&mutex);
}

/* Copy a page-aligned range; pages of zeros are not copied but punched out of the destination.
 * The merge buffer is mostly empty, and the sparse /dev/gfx keeps only written pages. 1 if any
 * page held something. */
static int sparse_copy(uint32_t to, uint32_t from, uint32_t size)
{
    uint8_t *mem = gfx_memory();
    int any = 0;
    for (uint32_t at = 0; at < size; at += PAGE) {
        uint32_t n = size - at < PAGE ? size - at : PAGE;
        const uint32_t *p = (const uint32_t *)(mem + from + at);
        unsigned k = 0;
        while (k < n / 4 && !p[k])
            k++;
        if (k < n / 4) {
            memcpy(mem + to + at, mem + from + at, n);
            any = 1;
        } else {
            gfx_zero(to + at, n);
        }
    }
    return any;
}

int gfx_merge_placeholder(int slot, const rect_t *sr, const surface_t *dst, const rect_t *dr,
                          uint32_t flags, const blit_settings_t *settings)
{
    struct merge *m;
    uint32_t size = (uint32_t)dst->pitch * dst->height;
    int blend = (flags & (BLIT_BLEND_ALPHACHANNEL | BLIT_BLEND_COLORALPHA)) != 0;
    pthread_mutex_lock(&mutex);
    m = find(dst->offset);
    if (!m && (m = find(0))) {
        memset(m, 0, sizeof *m);
        m->dst = dst->offset;
    }
    if (!m) {
        pthread_mutex_unlock(&mutex);
        gal_log("merge: no room to track the merge buffer 0x%x", dst->offset);
        return GAL_ERR;
    }
    if (m->under && m->size != size) {
        gfx_free(m->under);
        m->under = 0;
    }
    m->size = size;
    m->under_valid = 0;
    /* by copy (the compositor is the lowest window): nothing lies below it */
    if (blend) {
        if (!m->under) {
            uint32_t u = gfx_alloc(size);
            m->under = u == ~0u ? 0 : u;
        }
        if (m->under)
            m->under_valid = sparse_copy(m->under, dst->offset, size);
    }
    gfx_zero(dst->offset, size);
    m->slot = slot;
    m->src = *sr;
    m->dstr = *dr;
    m->alpha = (flags & BLIT_BLEND_COLORALPHA) && settings ? settings->alpha & 0xff : 255;
    pthread_mutex_unlock(&mutex);
    static int logged;
    if (gal_trace || logged++ < 3)
        gal_log(
            "merge: the compositor's window %d goes into 0x%x at %u,%u %ux%u (alpha %u), under %s",
            slot, dst->offset, dr->x, dr->y, dr->w, dr->h, m->alpha,
            m->under_valid ? "kept" : "empty");
    return GAL_OK;
}

void gfx_merge_lookup(uint32_t offset, struct gfx_controller_state *out)
{
    struct merge *m;
    memset(out, 0, sizeof *out);
    out->comp_window = -1;
    if (!gfx_display_process())
        return;
    pthread_mutex_lock(&mutex);
    m = find(offset);
    if (m && m->slot >= 0) {
        out->comp_window = m->slot;
        out->comp_src = m->src;
        out->comp_dst = m->dstr;
        out->comp_alpha = m->alpha;
        out->under = m->under_valid ? m->under : 0;
    }
    pthread_mutex_unlock(&mutex);
}
