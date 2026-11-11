// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* beanjar-vnc's frame rate above 60: kept while a frame's work fits in its time (rate.h). */
#include "rate.h"

void rate_init(struct rate *r, int hz)
{
    *r = (struct rate){.hz = hz, .decided = hz <= RATE_FLOOR, .last_enc = -1};
}

void rate_encoded(struct rate *r, unsigned frames, int64_t us)
{
    if (r->decided || r->seen < RATE_SKIP)
        return;
    r->n_enc += frames;
    r->enc_us += us;
}

int rate_frame(struct rate *r, int64_t main_us)
{
    if (r->decided)
        return 0;
    if (++r->seen <= RATE_SKIP)
        return 0;
    r->n++;
    r->main_us += main_us;
    if (r->n < RATE_WINDOW)
        return 0;
    int64_t budget = 1000000 / r->hz;
    r->last_main = r->main_us / r->n;
    r->last_enc = r->n_enc ? r->enc_us / r->n_enc : -1;
    int64_t worst = r->last_main > r->last_enc ? r->last_main : r->last_enc;
    r->strikes = worst > budget ? r->strikes + 1 : 0;
    r->n = r->n_enc = 0;
    r->main_us = r->enc_us = 0;
    if (r->strikes < RATE_STRIKES)
        return 0;
    /* what the slower stage allows, a tenth to spare; lower than now, not below the floor */
    int64_t fits = 1000000 * 10 / (worst * 11);
    int hz = fits >= r->hz ? r->hz - 1 : (int)fits;
    r->hz = hz < RATE_FLOOR ? RATE_FLOOR : hz;
    r->decided = r->hz <= RATE_FLOOR;
    r->seen = r->strikes = 0;
    return r->hz;
}
