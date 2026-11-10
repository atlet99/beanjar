// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_PCM_OUT_H
#define VIEWER_PCM_OUT_H
#include "audio.h"
#include "core.h"
#include "media.h"
#include <stddef.h>
/* The mixed audio for a front end that serves it itself (wv_audio_sink: beanjar-vnc's audio
 * socket). A thread of its own is the audio clock, every 10 ms: it consumes the media mix
 * (media_fill, media.c's mixer; this clock replaces media's clock thread), adds the guest's audio
 * from a FIFO that absorbs QEMU's timing (filled by audio.c's tap, converted and linearly resampled
 * to 48 kHz), and hands the sum to the sink as 48 kHz s16 stereo. While the sink wants nothing (no
 * listener) it still consumes the media mix on time and skips the rest. The GTK viewer and the
 * headless program never use it: their two SDL devices stay as they are. */
struct pcm_out;
struct pcm_out *pcm_out_start(struct media *media, wv_audio_fn fn, void *data, int wanted);
/* audio.c's tap, main thread: one Write in the guest's format; pcm NULL: the stream stopped (its
 * tail drains without counting an underrun). */
void pcm_out_guest(void *p, const struct audio_out *a, const unsigned char *pcm, size_t len);
void pcm_out_wanted(struct pcm_out *p, int wanted); /* any thread */
void pcm_out_stop(struct pcm_out *p);               /* joins the thread, prints its counters */
#endif
