// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* beanjar-vnc's frame rate above 60 (host/vnc/rate.c), at any panel rate (120, 144, 240): kept
 * while the frames' main loop work and their encoding fit in 1 s / hz on average; after
 * RATE_STRIKES windows in a row over it lowered to what the slower one allows (a tenth to spare,
 * lower than before, not below 60) and measured again there; the first RATE_SKIP frames at each
 * rate are not counted; 60 is never measured. */
#include "../../../host/vnc/rate.h"
#include <stdio.h>

static int failures;
static void check(int ok, const char *what, long a)
{
    if (!ok) {
        printf("FAIL %s: %ld\n", what, a);
        failures++;
    }
}

/* frames with main_us of main loop work and enc_us of encoding each (enc_us < 0: none encoded);
 * the frame at which the rate was lowered (0: it was not) */
static long feed(struct rate *r, long frames, long main_us, long enc_us)
{
    for (long i = 1; i <= frames; i++) {
        if (enc_us >= 0)
            rate_encoded(r, 1, enc_us);
        if (rate_frame(r, main_us))
            return i;
    }
    return 0;
}

int main(void)
{
    struct rate r;
    const long first = RATE_SKIP + RATE_STRIKES * RATE_WINDOW;
    /* well within the frame's time: kept, at 120, 144 and 240 */
    rate_init(&r, 120);
    check(!feed(&r, 3000, 4000, 6000) && r.hz == 120, "120 fits", r.hz);
    rate_init(&r, 144); /* 6.94 ms */
    check(!feed(&r, 3000, 3000, 6500) && r.hz == 144, "144 fits", r.hz);
    rate_init(&r, 240); /* 4.17 ms */
    check(!feed(&r, 3000, 2000, 4000) && r.hz == 240, "240 fits", r.hz);
    /* 120, encoding 10 ms: lowered to 90 (1 s / 11 ms), where it fits and stays */
    rate_init(&r, 120);
    long at = feed(&r, 3000, 3000, 10000);
    check(at == first && r.hz == 90, "slow encoder at 120", r.hz);
    check(r.last_enc == 10000 && r.last_main == 3000, "last window's means", r.last_enc);
    check(!feed(&r, 3000, 3000, 10000) && r.hz == 90 && !r.decided, "kept at 90", r.hz);
    /* 144, encoding 7.5 ms (over 6.94): lowered to 121, not to 120 or 60 */
    rate_init(&r, 144);
    check(feed(&r, 3000, 2000, 7500) && r.hz == 121, "144 lowered", r.hz);
    check(!feed(&r, 3000, 2000, 7500) && r.hz == 121, "kept at 121", r.hz);
    /* 240, main loop 5 ms: 181; then the encoder gets slower (7 ms): 129 */
    rate_init(&r, 240);
    check(feed(&r, 3000, 5000, -1) && r.hz == 181, "240 lowered", r.hz);
    check(feed(&r, 3000, 7000, -1) && r.hz == 129, "lowered again", r.hz);
    /* slower than 60 allows: the floor, for good */
    rate_init(&r, 144);
    check(feed(&r, 3000, 30000, 30000) && r.hz == 60 && r.decided, "floor", r.hz);
    check(!feed(&r, 3000, 100, 100) && r.hz == 60, "stays at the floor", r.hz);
    /* just over the budget: lowered by at least one */
    rate_init(&r, 144);
    check(feed(&r, 3000, 6950, -1) && r.hz < 144 && r.hz >= 60, "just over", r.hz);
    /* slow first frames (the GL's first draws) are skipped */
    rate_init(&r, 240);
    feed(&r, RATE_SKIP, 50000, 50000);
    check(!feed(&r, 3000, 2000, 2000) && r.hz == 240, "slow start skipped", r.hz);
    /* one window over, then one within: the count starts again */
    rate_init(&r, 120);
    feed(&r, RATE_SKIP, 1000, 1000);
    check(!feed(&r, RATE_WINDOW, 9000, 9000), "one window over", 0);
    check(!feed(&r, RATE_WINDOW, 1000, 1000), "then one within", 0);
    check(!feed(&r, RATE_WINDOW, 9000, 9000) && r.hz == 120, "one over again", r.hz);
    check(feed(&r, RATE_WINDOW, 9000, 9000) && r.hz == 101, "two in a row", r.hz);
    /* 60 (a 60 Hz panel, --fps 60) is never measured */
    rate_init(&r, 60);
    check(!feed(&r, 3000, 50000, 50000) && r.hz == 60 && r.decided, "60 is kept", r.hz);
    if (!failures)
        printf("ok: rate\n");
    return failures ? 1 : 0;
}
