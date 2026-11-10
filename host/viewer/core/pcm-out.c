// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
/* The mixed audio for wv_audio_sink (pcm-out.h): a 10 ms clock thread mixing the media audio
 * (media_fill) and the guest's (a FIFO fed by audio.c's tap), 48 kHz s16 stereo to the sink.
 * docs/dev/architecture/vnc.md, "Audio". */
#include "pcm-out.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RATE  48000
#define BLOCK 480 /* frames: 10 ms */
/* The guest FIFO, in 48 kHz frames: playback starts once PREFILL is queued; beyond CAP (QEMU's
 * bursts, or its clock drifting from ours) it drops back to TARGET, so latency never grows; when
 * it runs dry the block is padded with silence and it primes again. */
#define FIFO    RATE /* 1 s */
#define PREFILL (RATE * 40 / 1000)
#define TARGET  (RATE * 60 / 1000)
#define CAP     (RATE * 150 / 1000)
#define STALL   100000000ll /* ns behind the clock: restart it from now rather than catch up */

struct pcm_out {
    struct media *media;
    wv_audio_fn fn;
    void *data;
    volatile int wanted, quit;
    pthread_t thread;
    int running;
    pthread_mutex_t lock;
    /* the guest FIFO (lock): interleaved stereo float frames */
    float *fifo;
    unsigned head, count;
    int primed, ended;
    /* the resampler (main thread): the last input frame and the next output's position after
     * it, in units of 1/RATE of an input frame */
    float prev[2];
    uint64_t phase;
    unsigned in_rate;
    /* counters */
    unsigned long long blocks, sent, guest_in, guest_dropped, guest_full, underruns, stalls;
};

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/* Sample i of the guest's data as a float. */
static float sample(const struct audio_out *a, const unsigned char *p)
{
    unsigned n = a->bits / 8;
    uint32_t u = 0;
    for (unsigned k = 0; k < n; k++)
        u |= (uint32_t)p[a->be ? n - 1 - k : k] << (8 * k);
    if (a->is_float && n == 4) {
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
    if (!a->is_signed)
        u ^= 1u << (a->bits - 1);
    int32_t s = (int32_t)(u << (32 - a->bits));
    return (float)(s / 2147483648.0);
}

static void push(struct pcm_out *p, float l, float r)
{
    if (p->count == FIFO) {
        p->guest_full++;
        return;
    }
    unsigned at = (p->head + p->count) % FIFO;
    p->fifo[2 * at] = l;
    p->fifo[2 * at + 1] = r;
    p->count++;
}

void pcm_out_guest(void *data, const struct audio_out *a, const unsigned char *pcm, size_t len)
{
    struct pcm_out *p = data;
    if (!pcm) {
        pthread_mutex_lock(&p->lock);
        p->ended = 1;
        pthread_mutex_unlock(&p->lock);
        return;
    }
    if (!p->wanted || !a->freq || !a->frame ||
        (a->bits != 8 && a->bits != 16 && a->bits != 24 && a->bits != 32))
        return; /* nobody listens: no conversion */
    if (a->freq != p->in_rate) {
        p->in_rate = a->freq;
        p->phase = 0;
    }
    float g0 = a->mute ? 0 : a->gain[0], g1 = a->mute ? 0 : a->gain[1];
    size_t frames = len / a->frame;
    unsigned bytes = a->bits / 8;
    pthread_mutex_lock(&p->lock);
    p->ended = 0;
    p->guest_in += frames;
    for (size_t i = 0; i < frames; i++) {
        const unsigned char *f = pcm + i * a->frame;
        float cur[2] = {sample(a, f) * g0, sample(a, a->channels > 1 ? f + bytes : f) * g1};
        /* linear interpolation between prev and cur at each output instant in (prev, cur] */
        for (; p->phase <= RATE; p->phase += p->in_rate) {
            float t = (float)p->phase / RATE;
            push(p, p->prev[0] + (cur[0] - p->prev[0]) * t, p->prev[1] + (cur[1] - p->prev[1]) * t);
        }
        p->phase -= RATE;
        p->prev[0] = cur[0];
        p->prev[1] = cur[1];
    }
    pthread_mutex_unlock(&p->lock);
}

/* Adds the guest FIFO's next block to out. With p->lock held. */
static void guest(struct pcm_out *p, float *out)
{
    if (!p->primed && (p->count >= PREFILL || (p->ended && p->count)))
        p->primed = 1;
    if (!p->primed)
        return;
    if (p->count > CAP) {
        unsigned drop = p->count - TARGET;
        p->head = (p->head + drop) % FIFO;
        p->count -= drop;
        p->guest_dropped += drop;
    }
    unsigned n = p->count < BLOCK ? p->count : BLOCK;
    for (unsigned i = 0; i < n; i++) {
        unsigned at = (p->head + i) % FIFO;
        out[2 * i] += p->fifo[2 * at];
        out[2 * i + 1] += p->fifo[2 * at + 1];
    }
    p->head = (p->head + n) % FIFO;
    p->count -= n;
    if (n < BLOCK) {
        if (!p->ended)
            p->underruns++;
        p->primed = 0;
    }
}

static void *clock_thread(void *arg)
{
    struct pcm_out *p = arg;
    float mix[2 * BLOCK];
    short pcm[2 * BLOCK];
    int64_t next = now_ns();
    int was_wanted = 0;
    while (!p->quit) {
        int wanted = p->wanted;
        if (wanted && !was_wanted) {
            /* a first listener: the guest FIFO starts from what comes next */
            pthread_mutex_lock(&p->lock);
            p->count = 0;
            p->primed = 0;
            pthread_mutex_unlock(&p->lock);
        }
        was_wanted = wanted;
        /* the media mix is consumed on time whether or not anyone listens (it paces the media
         * helper's audio and the video's sync), as media's own clock thread did */
        if (p->media)
            media_fill(p->media, mix, BLOCK, next);
        else
            memset(mix, 0, sizeof mix);
        if (wanted) {
            pthread_mutex_lock(&p->lock);
            guest(p, mix);
            pthread_mutex_unlock(&p->lock);
            for (int i = 0; i < 2 * BLOCK; i++) {
                float x = mix[i] > 1 ? 1 : mix[i] < -1 ? -1 : mix[i];
                pcm[i] = (short)lrintf(x * 32767);
            }
            p->fn(p->data, pcm, BLOCK);
            p->sent++;
        }
        p->blocks++;
        next += 10000000;
        int64_t now = now_ns();
        if (now - next > STALL) {
            p->stalls++;
            next = now;
        }
        struct timespec ts = {next / 1000000000, next % 1000000000};
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR && !p->quit) {
        }
    }
    return NULL;
}

struct pcm_out *pcm_out_start(struct media *media, wv_audio_fn fn, void *data, int wanted)
{
    struct pcm_out *p = calloc(1, sizeof *p);
    p->media = media;
    p->fn = fn;
    p->data = data;
    p->wanted = wanted;
    p->fifo = calloc(2 * FIFO, sizeof(float));
    pthread_mutex_init(&p->lock, NULL);
    if (!pthread_create(&p->thread, NULL, clock_thread, p))
        p->running = 1;
    else
        fprintf(stderr, "audio out: cannot start the clock thread\n");
    fprintf(stderr, "audio out: 48 kHz s16 stereo, 10 ms blocks, guest FIFO %u/%u/%u ms\n",
            PREFILL * 1000 / RATE, TARGET * 1000 / RATE, CAP * 1000 / RATE);
    return p;
}

void pcm_out_wanted(struct pcm_out *p, int wanted)
{
    if (p)
        p->wanted = wanted;
}

void pcm_out_stop(struct pcm_out *p)
{
    if (!p)
        return;
    p->quit = 1;
    if (p->running)
        pthread_join(p->thread, NULL);
    fprintf(stderr,
            "audio out: blocks=%llu sent=%llu (%.2f s) guest_frames=%llu guest_dropped=%llu "
            "guest_overflow=%llu guest_underruns=%llu clock_stalls=%llu\n",
            p->blocks, p->sent, p->sent * (double)BLOCK / RATE, p->guest_in, p->guest_dropped,
            p->guest_full, p->underruns, p->stalls);
    pthread_mutex_destroy(&p->lock);
    free(p->fifo);
    free(p);
}
