// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libhal_vdec_direct for the emulator: the direct video decoder, whose stream goes to the host.
 *
 * LG's libdile_vdec_direct (stock, unchanged) is a thin layer over this HAL: it keeps the one
 * handle HAL_VDEC_DIRECT_Open returns and passes it to every other call. Its users are NDL
 * DirectMedia's first API (libNDL_directmedia_impl: NDL_DirectVideoOpen, Play, PlayWithCallback)
 * and libcgl. On the TV the HAL feeds the SoC's decoder through a ring and shows its pictures as
 * they come (the decoder runs free, no timestamps reach it); a buffer's ID given to
 * PlayWithCallback comes back through the callback, from a thread of the HAL's, once the decoder
 * is done with it.
 *
 * Here the access units go to the host decoder over a webos.media.<n> port (the soc layer's media
 * port, built without GStreamer: ../video/src/mediaport.c), untimed, which the host shows at once,
 * as the stock decoder does. A buffer's callback is called from this library's own thread after the
 * buffer has been sent. The stream is byte-stream H.264, HEVC or MPEG-2 video, as the stock HAL
 * takes (by its fourcc); NDL's first API opens H.264 only.
 *
 * It takes the stock library's place under its name (the users NEED libhal_vdec_direct.so) and
 * NEEDs the stock file, so a symbol not defined here still resolves to LG's code. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "../video/src/mediaport.h"

#define API __attribute__((visibility("default")))

typedef int DTV_STATUS_T; /* hal_common.h: OK 0, NOT_OK -1 */
#define OK     0
#define NOT_OK (-1)

#define FOURCC(a, b, c, d)                                                                         \
    ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define CALLBACKS 256 /* buffer IDs waiting for their callback */

typedef void (*buffid_cb)(unsigned long long buff_id);

enum codec { CODEC_H264, CODEC_HEVC, CODEC_MPEG2 };

struct HAL_DVDEC {
    struct media_port port;
    enum codec codec;
    int width, height;
    pthread_mutex_t lock; /* the callback and its queue */
    pthread_cond_t cond;
    pthread_t thread;
    gboolean have_thread, quit;
    buffid_cb cb;
    unsigned long long queue[CALLBACKS];
    unsigned head, tail;
    guint64 lost; /* callbacks dropped for a full queue */
};

/* The stock HAL serves one stream at a time. */
static pthread_mutex_t open_lock = PTHREAD_MUTEX_INITIALIZER;
static struct HAL_DVDEC *current;

static gint64 mono_clock(gpointer clk)
{
    (void)clk;
    return g_get_monotonic_time() * 1000;
}

/* Whether an access unit holds a picture to start decoding from: an IDR or SPS (H.264), an IRAP
 * or a parameter set (HEVC), a sequence header (MPEG-2). */
static gboolean keyframe(enum codec codec, const guint8 *d, gsize n)
{
    for (gsize i = 0; i + 3 < n; i++) {
        if (d[i] || d[i + 1] || d[i + 2] != 1)
            continue;
        guint8 b = d[i + 3];
        if (codec == CODEC_H264 && ((b & 0x1f) == 5 || (b & 0x1f) == 7))
            return TRUE;
        if (codec == CODEC_HEVC) {
            guint8 t = (b >> 1) & 0x3f;
            if ((t >= 16 && t <= 21) || (t >= 32 && t <= 34))
                return TRUE;
        }
        if (codec == CODEC_MPEG2 && b == 0xb3)
            return TRUE;
        i += 2;
    }
    return FALSE;
}

static void *callback_thread(void *data)
{
    struct HAL_DVDEC *v = data;
    pthread_mutex_lock(&v->lock);
    for (;;) {
        while (!v->quit && v->head == v->tail)
            pthread_cond_wait(&v->cond, &v->lock);
        if (v->head == v->tail)
            break;
        unsigned long long id = v->queue[v->tail % CALLBACKS];
        v->tail++;
        buffid_cb cb = v->cb;
        pthread_mutex_unlock(&v->lock);
        if (cb)
            cb(id);
        pthread_mutex_lock(&v->lock);
    }
    pthread_mutex_unlock(&v->lock);
    return NULL;
}

API struct HAL_DVDEC *HAL_VDEC_DIRECT_Open(unsigned int fourcc, int width, int height,
                                           int trid_type)
{
    struct HAL_DVDEC *v;
    enum codec codec;
    char caps[192];
    if (fourcc == FOURCC('h', '2', '6', '4'))
        codec = CODEC_H264;
    else if (fourcc == FOURCC('H', 'E', 'V', 'C'))
        codec = CODEC_HEVC;
    else if (fourcc == FOURCC('M', 'P', 'G', '2'))
        codec = CODEC_MPEG2;
    else {
        syslog(LOG_WARNING, "HAL_VDEC_DIRECT_Open: fourcc 0x%08x not supported", fourcc);
        return NULL;
    }
    pthread_mutex_lock(&open_lock);
    if (current) {
        pthread_mutex_unlock(&open_lock);
        syslog(LOG_WARNING, "HAL_VDEC_DIRECT_Open: already open");
        return NULL;
    }
    v = g_new0(struct HAL_DVDEC, 1);
    v->codec = codec;
    v->width = width;
    v->height = height;
    pthread_mutex_init(&v->lock, NULL);
    pthread_cond_init(&v->cond, NULL);
    media_port_init(&v->port);
    /* channel 0 until SetDEId names the decoder's port */
    media_port_open(&v->port, "vdec-direct", WEBOS_MEDIA_VIDEO, 0, 0, mono_clock, NULL);
    if (codec == CODEC_MPEG2)
        snprintf(caps, sizeof caps,
                 "video/mpeg, mpegversion=(int)2, systemstream=(boolean)false, width=(int)%d, "
                 "height=(int)%d",
                 width, height);
    else
        snprintf(caps, sizeof caps,
                 "%s, stream-format=(string)byte-stream, alignment=(string)au, width=(int)%d, "
                 "height=(int)%d",
                 codec == CODEC_H264 ? "video/x-h264" : "video/x-h265", width, height);
    media_port_caps_string(&v->port, NULL, caps, 0);
    struct webos_media_state s = {
        .playing = 1, .base_time = 0, .pause_rt = WEBOS_MEDIA_NONE, .rate = 1.0};
    media_port_send_state(&v->port, NULL, &s);
    v->have_thread = !pthread_create(&v->thread, NULL, callback_thread, v);
    current = v;
    pthread_mutex_unlock(&open_lock);
    syslog(LOG_INFO, "HAL_VDEC_DIRECT_Open: %s (3D %d) to the host", caps, trid_type);
    return v;
}

/* DILE passes the decoder's port here, the one the resource manager gave (NDL's VDEC port): the
 * scaler input the pictures reach. */
API DTV_STATUS_T HAL_VDEC_DIRECT_SetDEId(struct HAL_DVDEC *v, int deid)
{
    if (!v)
        return NOT_OK;
    media_port_set_channel(&v->port, deid >= 0 ? deid : 0);
    return OK;
}

static DTV_STATUS_T play(struct HAL_DVDEC *v, void *data, int size, gboolean callback,
                         unsigned long long id)
{
    if (!v || !data || size <= 0)
        return NOT_OK;
    struct webos_media_buffer b = {.pts = WEBOS_MEDIA_NONE,
                                   .dts = WEBOS_MEDIA_NONE,
                                   .duration = WEBOS_MEDIA_NONE,
                                   .stream_time = WEBOS_MEDIA_NONE};
    if (keyframe(v->codec, data, (gsize)size))
        b.flags |= WEBOS_MEDIA_KEYFRAME;
    media_port_send(&v->port, NULL, &b, data, (gsize)size);
    if (callback) {
        pthread_mutex_lock(&v->lock);
        if (v->head - v->tail < CALLBACKS) {
            v->queue[v->head % CALLBACKS] = id;
            v->head++;
            pthread_cond_signal(&v->cond);
        } else if (!v->lost++)
            syslog(LOG_WARNING, "HAL_VDEC_DIRECT: callback queue full, buffer IDs dropped");
        pthread_mutex_unlock(&v->lock);
    }
    return OK;
}

API DTV_STATUS_T HAL_VDEC_DIRECT_Play(struct HAL_DVDEC *v, void *data, int size)
{
    return play(v, data, size, FALSE, 0);
}

API DTV_STATUS_T HAL_VDEC_DIRECT_PlayWithCallback(struct HAL_DVDEC *v, void *data, int size,
                                                  unsigned long long buff_id)
{
    return play(v, data, size, TRUE, buff_id);
}

API DTV_STATUS_T HAL_VDEC_DIRECT_SetCallback(struct HAL_DVDEC *v, buffid_cb cb)
{
    if (!v)
        return NOT_OK;
    pthread_mutex_lock(&v->lock);
    v->cb = cb;
    pthread_mutex_unlock(&v->lock);
    return OK;
}

API DTV_STATUS_T HAL_VDEC_DIRECT_Stop(struct HAL_DVDEC *v)
{
    if (!v)
        return NOT_OK;
    media_port_event(&v->port, NULL, WEBOS_MEDIA_FLUSH);
    return OK;
}

API DTV_STATUS_T HAL_VDEC_DIRECT_Close(struct HAL_DVDEC *v)
{
    if (!v)
        return NOT_OK;
    pthread_mutex_lock(&open_lock);
    if (current == v)
        current = NULL;
    pthread_mutex_unlock(&open_lock);
    pthread_mutex_lock(&v->lock);
    v->quit = TRUE;
    pthread_cond_signal(&v->cond);
    pthread_mutex_unlock(&v->lock);
    if (v->have_thread)
        pthread_join(v->thread, NULL);
    media_port_stop(&v->port, NULL);
    media_port_clear(&v->port);
    pthread_cond_destroy(&v->cond);
    pthread_mutex_destroy(&v->lock);
    syslog(LOG_INFO, "HAL_VDEC_DIRECT_Close");
    g_free(v);
    return OK;
}

/* The stream's information comes from the decoder, which is the host's: not known here. Nothing
 * on the firmwares studied calls it (NDL's first API does not). */
API DTV_STATUS_T HAL_VDEC_DIRECT_GetVideoInfo(struct HAL_DVDEC *v, void *info)
{
    (void)v;
    (void)info;
    return NOT_OK;
}
