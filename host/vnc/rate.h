// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNC_RATE_H
#define VNC_RATE_H
/* Whether beanjar-vnc sustains its frame rate above 60 (docs/dev/architecture/vnc.md, "Frame
 * rate"): a fast panel's picture (120, 144, 240 Hz: any rate) is made at the panel's rate only
 * while a frame's work fits in the frame's time. The main loop's work for a frame (composing, the
 * read backs or the GBM render's fence, the NV12 conversion) and the video encoder thread's (one
 * frame encoded) are each added up over windows of RATE_WINDOW frames, after the first RATE_SKIP
 * (the GL's and the encoder's first frames are slow on every host); a window whose mean of either
 * is over the frame's time (1 s / hz) counts against it, one within it clears the count, and
 * RATE_STRIKES in a row lower the rate for the rest of the session to what the slower mean
 * allows, with a tenth to spare (never below RATE_FLOOR, the UI's own rate, and always lower than
 * before), after which the new rate is measured the same way. No frame is ever repeated to fill a
 * rate. */
#include <stdint.h>

#define RATE_WINDOW  60
#define RATE_SKIP    30
#define RATE_STRIKES 2
#define RATE_FLOOR   60

struct rate {
    int hz;         /* the rate now */
    int decided;    /* at the floor (or never above it): no more measuring */
    unsigned seen;  /* frames counted at this rate, the skipped ones too */
    unsigned n;     /* this window's main loop frames */
    unsigned n_enc; /* its encoded frames */
    int64_t main_us, enc_us;
    unsigned strikes;
    /* the last window's means (microseconds a frame; enc -1: nothing encoded) */
    int64_t last_main, last_enc;
};

void rate_init(struct rate *r, int hz);
/* One frame made, its main loop work in us. Returns the new rate when it is lowered, else 0. */
int rate_frame(struct rate *r, int64_t main_us);
/* Frames encoded since the last call, and their encoder time in us (the encoder thread's counters'
 * differences; called before rate_frame). */
void rate_encoded(struct rate *r, unsigned frames, int64_t us);
#endif
