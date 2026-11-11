// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_PACE_H
#define VIEWER_PACE_H
/* An output rate's pacing (docs/dev/architecture/viewer.md, "Output rate"): the times a picture
 * may be made at, at most hz a second. The core gates its "frame changed" signal with one (the
 * GTK viewer, viewer-headless), beanjar-vnc its renders. Times are monotonic microseconds.
 *
 * The due times lie on a grid of the period, so a picture made a little late (the 4 ms poll, a
 * busy main loop) lets the next one come that much sooner and the rate holds; after a pause the
 * grid restarts, never closer than half a period to the last picture, so no burst catches up.
 * In any window of T microseconds at most T / period + 2 pictures are made. hz 0: unpaced. */
#include <stdint.h>

/* the panel's refresh where vm-info.toml gives none; the most an option may ask for (a sanity
 * bound on input, not a cap of the panel's rate: 1 ms is the main loop's timer step) */
#define PANEL_HZ_DEFAULT 60
#define OUTPUT_HZ_LIMIT  1000

struct pace {
    int64_t period; /* microseconds; 0: unpaced */
    int64_t next;   /* the next picture's due time; 0: none made yet */
};

static inline void pace_set(struct pace *p, int hz)
{
    p->period = hz > 0 ? 1000000 / hz : 0;
    p->next = 0;
}
/* The poll's step in ms for hz: half a period, so that a picture due between two polls is late by
 * at most that and the grid makes it up, at most the core's usual 4 ms, at least the main loop
 * timer's 1 ms (above 500 Hz pictures come on 1 ms polls, the mean rate still held). */
static inline int pace_poll_ms(int hz)
{
    int ms = hz > 0 ? 500 / hz : 4;
    return ms > 4 ? 4 : ms < 1 ? 1 : ms;
}
/* how long until a picture may be made (0: now) */
static inline int64_t pace_wait(const struct pace *p, int64_t now)
{
    return p->period && p->next > now ? p->next - now : 0;
}
/* a picture was made at now */
static inline void pace_mark(struct pace *p, int64_t now)
{
    if (!p->period)
        return;
    int64_t grid = p->next + p->period, soonest = now + p->period / 2;
    p->next = p->next && grid > soonest ? grid : soonest;
}
#endif
