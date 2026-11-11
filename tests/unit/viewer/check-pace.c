// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The output rate's pacing (host/viewer/core/pace.h): pictures asked for on a coarse poll (the
 * core's 4 ms, a busy loop's 1 ms) are made at most hz a second in every second (60, 120, 144 and
 * 240: rates that are no multiple of 60 too), the mean rate
 * holds at hz when every poll asks, a pause does not let a burst catch up, and 0 is unpaced. */
#include "../../../host/viewer/core/pace.h"
#include <stdio.h>

static int failures;
static void check(int ok, const char *what, long a, long b)
{
    if (!ok) {
        printf("FAIL %s: %ld, %ld\n", what, a, b);
        failures++;
    }
}

/* polls every step us for total us, a picture whenever one may be made; the most made in any
 * one-second window, and the count in all */
static long run(int hz, long step, long total, long *most)
{
    static long times[200000];
    struct pace p;
    pace_set(&p, hz);
    long n = 0;
    for (long t = 1000000; t < 1000000 + total; t += step)
        if (!pace_wait(&p, t)) {
            pace_mark(&p, t);
            times[n++] = t;
        }
    *most = 0;
    for (long i = 0, j = 0; i < n; i++) {
        while (times[i] - times[j] >= 1000000)
            j++;
        if (i - j + 1 > *most)
            *most = i - j + 1;
    }
    return n;
}

int main(void)
{
    const int rates[] = {60, 120, 144, 240};
    for (unsigned r = 0; r < 4; r++)
        for (unsigned s = 0; s < 2; s++) {
            /* the core's poll for the rate (pace_poll_ms), and a busy loop's 1 ms */
            long step = s ? 1000 : pace_poll_ms(rates[r]) * 1000;
            long most, n = run(rates[r], step, 10000000, &most);
            /* at most hz + 2 in any second (pace.h), and the rate kept: 10 s at hz within 2 % */
            check(most <= rates[r] + 2, "most in a second", most, rates[r]);
            check(n >= rates[r] * 10 * 98 / 100, "mean rate", n, rates[r] * 10);
        }
    /* unpaced: every poll */
    long most, n = run(0, 4000, 1000000, &most);
    check(n == 250, "unpaced", n, 250);
    /* after a pause the next picture comes at once, the one after it no sooner than half a period
     */
    struct pace p;
    pace_set(&p, 120);
    pace_mark(&p, 1000000);
    check(pace_wait(&p, 1000000 + 4000) > 0, "half a period after the first", 0, 0);
    check(pace_wait(&p, 5000000) == 0, "after a pause", 0, 0);
    pace_mark(&p, 5000000);
    check(pace_wait(&p, 5000000 + 4000) > 0, "no burst after a pause", pace_wait(&p, 5004000), 0);
    check(pace_poll_ms(60) == 4 && pace_poll_ms(120) == 4 && pace_poll_ms(144) == 3 &&
              pace_poll_ms(240) == 2 && pace_poll_ms(1000) == 1 && pace_poll_ms(0) == 4,
          "poll steps", pace_poll_ms(144), pace_poll_ms(240));
    if (!failures)
        printf("ok: pace\n");
    return failures ? 1 : 0;
}
