// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libhal_audio_direct for the emulator: the direct audio output, whose stream goes to the host.
 *
 * LG's libdile_audio_direct (stock, unchanged) keeps one context of this HAL per channel (main,
 * sub) and passes it to every call; it turns its sampling-frequency enum into Hz for Start. Its
 * users are NDL DirectMedia's first API (NDL_DirectAudioOpen, Play, CheckBufferSize, Pause,
 * Resume, Close), libcgl and SDL's webOS audio. On the TV, Write copies into the decoder's
 * (ADEC0..3) or mixer's (4..11) ring without waiting, which plays it as it comes; a mixer takes PCM
 * only. With no room an ADEC's write still answers OK (the data is dropped) and a mixer's fails;
 * GetBufferInfo reports the ring's size and free bytes.
 *
 * Here the stream goes to the host over a webos.media.<n> port (../video/src/mediaport.c, built
 * without GStreamer), stamped as the media sinks stamp audio whose sync is off, each buffer after
 * the one before, except that one which finds nothing queued starts the low-delay window's lower
 * bound after its arrival (SetNoDelayParam), where the DSP would start playing. Write always
 * answers OK and sends everything; the host plays it in time. GetBufferInfo reports a ring of
 * RING_BYTES holding what is still to play by those stamps, so an app that paces itself on it sees
 * the ring empty in real time.
 *
 * It takes the stock library's place under its name and NEEDs the stock file, so a symbol not
 * defined here still resolves to LG's code. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "../video/src/mediaport.h"

#define API __attribute__((visibility("default")))

typedef int DTV_STATUS_T; /* hal_common.h: OK 0, NOT_OK -1 */
#define OK     0
#define NOT_OK (-1)

enum { /* HAL_AUDIO_DIRECT_CODEC_TYPE_T */
       CODEC_PCM = 1,
       CODEC_AC3 = 2,
       CODEC_AAC = 3,
       CODEC_MP3 = 5,
};
#define INDEX_MAX   11 /* HAL_AUDIO_DIRECT_MIXER7 */
#define FIRST_MIXER 4  /* HAL_AUDIO_DIRECT_MIXER0 */
#define RING_BYTES  (256u << 10)
#define NS          1000000000ll
#define LEAD_MS     100 /* the low-delay window's lower bound when the app sets none */

struct audio_direct {
    struct media_port port;
    int index;
    gboolean started, paused;
    int codec, rate, channels, bits;
    gint64 bytes_per_second;    /* PCM's; 0 for compressed */
    gint64 base;                /* the session's base time on the monotonic clock */
    gint64 pause_rt;            /* where a paused session stopped */
    gint64 next;                /* where the next buffer starts, or NONE */
    gint64 sent_bytes, sent_ns; /* what was stamped, for the ring's fill of compressed audio */
    gint64 lead;                /* how far ahead of its arrival a buffer after a gap starts */
};

static gint64 mono_clock(gpointer clk)
{
    (void)clk;
    return g_get_monotonic_time() * 1000;
}

static gint64 running_time(struct audio_direct *a)
{
    return a->paused ? a->pause_rt : mono_clock(NULL) - a->base;
}

static void send_state(struct audio_direct *a)
{
    struct webos_media_state s = {.playing = !a->paused,
                                  .base_time = a->base,
                                  .pause_rt = a->paused ? a->pause_rt : WEBOS_MEDIA_NONE,
                                  .rate = 1.0};
    media_port_send_state(&a->port, NULL, &s);
}

/* ADTS frames of 1024 samples each in an AAC buffer: its play time; NONE without a whole frame */
static gint64 adts_duration(const guint8 *d, gsize n, int rate)
{
    gint64 frames = 0;
    gsize i = 0;
    while (i + 7 <= n && d[i] == 0xff && (d[i + 1] & 0xf6) == 0xf0) {
        gsize len = (gsize)(d[i + 3] & 3) << 11 | (gsize)d[i + 4] << 3 | d[i + 5] >> 5;
        if (len < 7)
            break;
        frames += (d[i + 6] & 3) + 1;
        i += len;
    }
    return frames && rate > 0 ? frames * 1024 * NS / rate : WEBOS_MEDIA_NONE;
}

API void *HAL_AUDIO_DIRECT_Open(int index)
{
    if (index < 0 || index > INDEX_MAX)
        return NULL;
    struct audio_direct *a = g_new0(struct audio_direct, 1);
    a->index = index;
    a->next = WEBOS_MEDIA_NONE;
    a->lead = LEAD_MS * 1000000ll;
    media_port_init(&a->port);
    syslog(LOG_INFO, "HAL_AUDIO_DIRECT_Open: index %d", index);
    return a;
}

API DTV_STATUS_T HAL_AUDIO_DIRECT_Start(void *ctx, int codec, unsigned int rate,
                                        unsigned int channels, unsigned int bits)
{
    struct audio_direct *a = ctx;
    char caps[192];
    const char *format;
    if (!a || !rate || !channels)
        return NOT_OK;
    if (a->index >= FIRST_MIXER && codec != CODEC_PCM) {
        syslog(LOG_WARNING, "HAL_AUDIO_DIRECT_Start: mixer %d takes PCM only, not %d", a->index,
               codec);
        return NOT_OK;
    }
    switch (codec) {
    case CODEC_PCM:
        format = bits == 8    ? "S8"
                 : bits == 16 ? "S16LE"
                 : bits == 24 ? "S24LE"
                 : bits == 32 ? "S32LE"
                              : NULL;
        if (!format)
            return NOT_OK;
        snprintf(caps, sizeof caps,
                 "audio/x-raw, format=(string)%s, layout=(string)interleaved, rate=(int)%u, "
                 "channels=(int)%u",
                 format, rate, channels);
        break;
    case CODEC_AC3:
        snprintf(caps, sizeof caps, "audio/x-ac3, rate=(int)%u, channels=(int)%u", rate, channels);
        break;
    case CODEC_AAC:
        snprintf(caps, sizeof caps,
                 "audio/mpeg, mpegversion=(int)4, stream-format=(string)adts, rate=(int)%u, "
                 "channels=(int)%u",
                 rate, channels);
        break;
    case CODEC_MP3:
        snprintf(caps, sizeof caps,
                 "audio/mpeg, mpegversion=(int)1, layer=(int)3, rate=(int)%u, channels=(int)%u",
                 rate, channels);
        break;
    default:
        syslog(LOG_WARNING, "HAL_AUDIO_DIRECT_Start: codec %d not supported", codec);
        return NOT_OK;
    }
    if (a->started)
        media_port_stop(&a->port, NULL);
    if (!media_port_open(&a->port, "audio-direct", WEBOS_MEDIA_AUDIO, a->index, 0, mono_clock,
                         NULL))
        return NOT_OK;
    a->codec = codec;
    a->rate = (int)rate;
    a->channels = (int)channels;
    a->bits = (int)bits;
    a->bytes_per_second = codec == CODEC_PCM ? (gint64)rate * channels * (bits / 8) : 0;
    media_port_caps_string(&a->port, NULL, caps, a->bytes_per_second);
    a->base = mono_clock(NULL);
    a->paused = FALSE;
    a->next = WEBOS_MEDIA_NONE;
    a->sent_bytes = a->sent_ns = 0;
    a->started = TRUE;
    send_state(a);
    syslog(LOG_INFO, "HAL_AUDIO_DIRECT_Start: index %d, %s to the host", a->index, caps);
    return OK;
}

API DTV_STATUS_T HAL_AUDIO_DIRECT_Write(void *ctx, void *buf, unsigned int size)
{
    struct audio_direct *a = ctx;
    if (!a || !buf || !size)
        return NOT_OK;
    if (!a->started)
        return NOT_OK;
    gint64 now = running_time(a), duration = WEBOS_MEDIA_NONE;
    struct webos_media_buffer b = {
        .dts = WEBOS_MEDIA_NONE, .stream_time = WEBOS_MEDIA_NONE, .flags = WEBOS_MEDIA_KEYFRAME};
    b.pts = a->next != WEBOS_MEDIA_NONE && a->next > now ? a->next : now + a->lead;
    if (a->bytes_per_second > 0)
        duration = (gint64)size * NS / a->bytes_per_second;
    else if (a->codec == CODEC_AAC)
        duration = adts_duration(buf, size, a->rate);
    b.duration = duration;
    a->next = duration != WEBOS_MEDIA_NONE ? b.pts + duration : WEBOS_MEDIA_NONE;
    if (duration != WEBOS_MEDIA_NONE) {
        a->sent_bytes += size;
        a->sent_ns += duration;
    }
    media_port_send(&a->port, NULL, &b, buf, size);
    return OK;
}

API DTV_STATUS_T HAL_AUDIO_DIRECT_GetBufferInfo(void *ctx, unsigned int *max, unsigned int *free_)
{
    struct audio_direct *a = ctx;
    gint64 queued = 0, rate;
    if (!a || !max || !free_)
        return NOT_OK;
    rate = a->bytes_per_second > 0 ? a->bytes_per_second
           : a->sent_ns > 0        ? a->sent_bytes * NS / a->sent_ns
                                   : 0;
    if (a->started && a->next != WEBOS_MEDIA_NONE && rate > 0) {
        gint64 ahead = a->next - running_time(a);
        if (ahead > 0)
            queued = ahead * rate / NS;
    }
    if (queued > RING_BYTES)
        queued = RING_BYTES;
    *max = RING_BYTES;
    *free_ = RING_BYTES - (unsigned int)queued;
    return OK;
}

API DTV_STATUS_T HAL_AUDIO_DIRECT_GetRawBufferInfo(void *ctx, unsigned int *max,
                                                   unsigned int *free_)
{
    return HAL_AUDIO_DIRECT_GetBufferInfo(ctx, max, free_);
}

API DTV_STATUS_T HAL_AUDIO_DIRECT_Pause(void *ctx)
{
    struct audio_direct *a = ctx;
    if (!a)
        return NOT_OK;
    if (a->started && !a->paused) {
        a->pause_rt = running_time(a);
        a->paused = TRUE;
        send_state(a);
    }
    return OK;
}

API DTV_STATUS_T HAL_AUDIO_DIRECT_Resume(void *ctx)
{
    struct audio_direct *a = ctx;
    if (!a)
        return NOT_OK;
    if (a->started && a->paused) {
        /* the session's time goes on from where it stopped: what was queued plays after */
        a->base = mono_clock(NULL) - a->pause_rt;
        a->paused = FALSE;
        send_state(a);
    }
    return OK;
}

API DTV_STATUS_T HAL_AUDIO_DIRECT_Flush(void *ctx)
{
    struct audio_direct *a = ctx;
    if (!a)
        return NOT_OK;
    a->next = WEBOS_MEDIA_NONE;
    if (a->started)
        media_port_event(&a->port, NULL, WEBOS_MEDIA_FLUSH);
    return OK;
}

API DTV_STATUS_T HAL_AUDIO_DIRECT_Stop(void *ctx)
{
    struct audio_direct *a = ctx;
    if (!a)
        return NOT_OK;
    if (a->started) {
        media_port_stop(&a->port, NULL);
        a->started = FALSE;
    }
    a->next = WEBOS_MEDIA_NONE;
    return OK;
}

API DTV_STATUS_T HAL_AUDIO_DIRECT_Close(void *ctx)
{
    struct audio_direct *a = ctx;
    if (!a)
        return NOT_OK;
    HAL_AUDIO_DIRECT_Stop(a);
    media_port_clear(&a->port);
    syslog(LOG_INFO, "HAL_AUDIO_DIRECT_Close: index %d", a->index);
    g_free(a);
    return OK;
}

/* The DSP's low-delay window (ms): it keeps between lower and upper queued (NDL's default 100 and
 * 150). A buffer that finds nothing queued starts lower after its arrival, as the DSP starts once
 * it holds that much; LEAD_MS without a window. */
API DTV_STATUS_T HAL_AUDIO_DIRECT_SetNoDelayParam(void *ctx, unsigned int on, unsigned int upper,
                                                  unsigned int lower)
{
    struct audio_direct *a = ctx;
    if (!a)
        return NOT_OK;
    a->lead = (on && lower ? lower : LEAD_MS) * 1000000ll;
    syslog(LOG_INFO, "HAL_AUDIO_DIRECT_SetNoDelayParam: index %d %s (%u, %u)", a->index,
           on ? "on" : "off", upper, lower);
    return OK;
}
