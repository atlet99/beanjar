// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
/* beanjar-vnc's video socket (video.h): the request on the main loop; the H.264 stream's
 * encoding and sending on a thread of its own, which owns the clients and never waits on one. */
#include "video.h"
#include "h264v4l2.h"
#include "yuv.h"
#include <errno.h>
#include <glib.h>
#include <glib-unix.h>
#include <glob.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define HEADER          32
#define UNIT_HEAD       16
#define CODEC_H264      1
#define FLAG_KEY        1
#define BITRATE_DEFAULT 10000 /* kbit/s: 720p60, the most */
#define QUALITY         23    /* NVENC's cq, x264's crf */
#define BITRATE_MIN     100
#define BITRATE_MAX     50000
#define SNDBUF          (256 * 1024)
#define SLOW_US         3000000 /* nothing taken for this long: disconnected */
#define IDLE_US         1000000 /* a still picture: encoded again this often */
#define FORCE_GAP_US    250000  /* keyframes forced for slow clients: at most this often */
#define REQUEST_MS      5000
#define MAX_CLIENTS     16

enum { ENC_NVENC, ENC_VAAPI, ENC_V4L2, ENC_X264, ENC_COUNT };
static const char *const enc_names[ENC_COUNT] = {"nvenc", "vaapi", "v4l2", "x264"};
/* libavcodec's, except v4l2's (h264v4l2.c, its own V4L2 calls) */
static const char *const enc_codecs[ENC_COUNT] = {"h264_nvenc", "h264_vaapi", NULL, "libx264"};

struct client {
    struct client *next;
    struct video_server *s;
    struct stream *st;
    int fd, param; /* the bitrate it asked for (kbit/s) */
    char req[96];
    unsigned reqlen;
    guint watch, timeout;
    /* the encoder thread's: the rest of a unit a short write left, waiting for a keyframe, the
     * first unit dropped in a row (monotonic us; 0: none) */
    unsigned char *pend;
    size_t plen, poff, pcap;
    int wait_key, starved, ready, dead;
    gint64 since;
};

/* The clients and their encoder. */
struct stream {
    struct video_server *s;
    int efd;
    pthread_t thread;
    pthread_mutex_t lock;
    /* the frames' buffers, reference-counted: the slot's, the work's, and those libavcodec
     * still holds (NVENC's input, until it is uploaded); one is reused only once all let it go */
    AVBufferPool *pool;
    /* lock: clients handed to the thread, the latest frame, the end */
    struct client *incoming;
    AVBufferRef *slot;
    gint64 slot_time;
    int slot_full, slot_nv12, stop; /* slot_nv12: the slot's buffer is NV12 (video_nv12) */
    gint active;                    /* clients streaming or handed over (atomic) */
    gint takes_nv12;                /* the open encoder takes NV12 from the GPU (atomic) */
    AVBufferPool *nv12_pool;        /* their buffers (nv12_size bytes) */
    /* the encoder thread's: chosen, ENC_* (-1: auto, not yet found) */
    struct client *clients;
    AVBufferRef *work;
    gint64 work_time, last_encode, last_forced;
    int have_work, work_nv12, chosen, open, force;
    AVCodecContext *ctx;
    AVFrame *frame, *sw;
    AVPacket *pkt;
    AVBufferRef *hwdev, *hwframes;
    struct SwsContext *sws;
    struct h264v4l2 *v4l2;
    unsigned long long served, refused, frames, keys, bytes, dropped, slow, opens, force_count;
    /* new pictures encoded and the thread's time on them (us; atomic: video_server_encoded) */
    unsigned long long fresh_n, fresh_us;
};

struct video_server {
    char *path;
    int fd, width, height, fps;
    /* GBM frames' bytes a row (video_server_gbm_frame: dma-bufs, B, G, R, X), or 0: RGBX bytes
     * copied in (video_server_frame); gbm: one of them, for the encoders' setup */
    unsigned dmabuf;
    struct vnc_frame gbm;
    /* the GPU converts to NV12 (video_server_new's nv12; atomic: off once a conversion failed),
     * the encoders' size (even) and its rows' bytes */
    gint nv12;
    int nv12_w, nv12_h, nv12_stride;
    guint watch;
    void (*wake)(void *);
    void *data;
    struct client *requesting; /* main thread */
    gint64 origin;
    unsigned long long refused; /* main thread: requests refused before a stream took them */
    struct stream stream;
};

static int named(const char *const *names, int count, const char *name)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(name, names[i]))
            return i;
    return -1;
}

int video_encoder_known(const char *name)
{
    return !strcmp(name, "auto") || named(enc_names, ENC_COUNT, name) >= 0;
}

static const char *encoder_name(const struct stream *st)
{
    return st->chosen < 0 ? "none" : enc_names[st->chosen];
}

static void client_free(struct client *c)
{
    if (c->watch)
        g_source_remove(c->watch);
    if (c->timeout)
        g_source_remove(c->timeout);
    close(c->fd);
    free(c->pend);
    free(c);
}

/* -- the encoders (a stream's thread) -- */

static void h264_close(struct stream *s)
{
    h264v4l2_close(s->v4l2);
    s->v4l2 = NULL;
    avcodec_free_context(&s->ctx);
    av_frame_free(&s->frame);
    av_frame_free(&s->sw);
    av_packet_free(&s->pkt);
    av_buffer_unref(&s->hwframes);
    av_buffer_unref(&s->hwdev);
    sws_freeContext(s->sws);
    s->sws = NULL;
    s->open = 0;
    g_atomic_int_set(&s->takes_nv12, 0);
}

static const char *av_error(int err)
{
    static char buf[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(buf, sizeof buf, err);
}

/* The first render node, for VA-API; NULL: none. */
static char *render_node(void)
{
    glob_t g;
    char *node = NULL;
    if (!glob("/dev/dri/renderD*", 0, NULL, &g) && g.gl_pathc)
        node = strdup(g.gl_pathv[0]);
    globfree(&g);
    return node;
}

/* video_vaapi_prepare's device (WSL), for every VA-API encoder opened */
static AVBufferRef *va_device;

int video_vaapi_prepare(const char *adapter)
{
    if (access("/dev/dxg", F_OK))
        return 0; /* not WSL: the system's VA driver, opened with the encoder */
    char *node = render_node();
    if (!node) {
        fprintf(stderr, "beanjar-vnc: video: h264_vaapi: no render node (/dev/dri/renderD*; on "
                        "WSL: sudo modprobe vgem)\n");
        return -1;
    }
    static const char *const names[] = {"LIBVA_DRIVER_NAME", "GALLIUM_DRIVER",
                                        "MESA_D3D12_DEFAULT_ADAPTER_NAME"};
    const char *values[] = {"d3d12", "d3d12", adapter};
    int set[3] = {0};
    for (int i = 0; i < 3; i++)
        if (values[i] && !getenv(names[i]))
            set[i] = !setenv(names[i], values[i], 1);
    int err = av_hwdevice_ctx_create(&va_device, AV_HWDEVICE_TYPE_VAAPI, node, NULL, 0);
    for (int i = 0; i < 3; i++)
        if (set[i])
            unsetenv(names[i]);
    if (err < 0) {
        fprintf(stderr, "beanjar-vnc: video: h264_vaapi: %s (d3d12%s%s): %s\n", node,
                adapter ? ", adapter " : "", adapter ? adapter : "", av_error(err));
        free(node);
        return -1;
    }
    fprintf(stderr, "beanjar-vnc: video: VA-API on %s with Mesa's d3d12 driver%s%s\n", node,
            adapter ? ", adapter " : "", adapter ? adapter : "");
    free(node);
    return 0;
}

/* One encoder opened at bitrate (kbit/s); 0, or a negative AVERROR with the reason printed. */
static int h264_try(struct stream *s, int which, int bitrate)
{
    if (which == ENC_V4L2) {
        struct video_server *v = s->s;
        if (!(s->v4l2 = h264v4l2_open(v->width, v->height, v->fps, bitrate, v->dmabuf)))
            return AVERROR(ENODEV);
        s->open = bitrate;
        s->opens++;
        fprintf(stderr, "beanjar-vnc: video encoder v4l2 (%s), %dx%d, up to %d fps, %d kbit/s\n",
                h264v4l2_detail(s->v4l2), v->width, v->height, v->fps, bitrate);
        return 0;
    }
    const AVCodec *codec = avcodec_find_encoder_by_name(enc_codecs[which]);
    if (!codec) {
        fprintf(stderr, "beanjar-vnc: video: no %s in this libavcodec\n", enc_codecs[which]);
        return AVERROR_ENCODER_NOT_FOUND;
    }
    int w = s->s->width & ~1, h = s->s->height & ~1, err;
    /* x264 and VA-API take NV12 converted on the GPU, unless it cannot (then swscale's YUV) */
    int gpu = which != ENC_NVENC && g_atomic_int_get(&s->s->nv12);
    /* the frames' bytes: RGBX, or B, G, R, X in GBM frames */
    enum AVPixelFormat rgb = s->s->dmabuf ? AV_PIX_FMT_BGR0 : AV_PIX_FMT_RGB0;
    char *node = NULL;
    if (which == ENC_VAAPI && va_device) {
        s->hwdev = av_buffer_ref(va_device);
    } else if (which == ENC_VAAPI) {
        node = render_node();
        if (!node) {
            fprintf(stderr, "beanjar-vnc: video: h264_vaapi: no render node (/dev/dri/renderD*)\n");
            return AVERROR(ENODEV);
        }
        err = av_hwdevice_ctx_create(&s->hwdev, AV_HWDEVICE_TYPE_VAAPI, node, NULL, 0);
        if (err < 0) {
            fprintf(stderr, "beanjar-vnc: video: h264_vaapi: %s: %s\n", node, av_error(err));
            free(node);
            return err;
        }
        free(node);
    }
    if (which == ENC_VAAPI) {
        s->hwframes = av_hwframe_ctx_alloc(s->hwdev);
        AVHWFramesContext *fc = (AVHWFramesContext *)s->hwframes->data;
        fc->format = AV_PIX_FMT_VAAPI;
        fc->sw_format = AV_PIX_FMT_NV12;
        fc->width = w;
        fc->height = h;
        fc->initial_pool_size = 4;
        if ((err = av_hwframe_ctx_init(s->hwframes)) < 0) {
            fprintf(stderr, "beanjar-vnc: video: h264_vaapi: frames: %s\n", av_error(err));
            h264_close(s);
            return err;
        }
    }
    AVCodecContext *c = s->ctx = avcodec_alloc_context3(codec);
    c->width = w;
    c->height = h;
    c->time_base = (AVRational){1, 1000000};
    c->framerate = (AVRational){s->s->fps, 1};
    c->max_b_frames = 0;
    c->gop_size = s->s->fps * 10; /* long: keyframes come on request */
    /* the bitrate is a cap (VBV of half a second), not a target: constant quality below it, so a
     * still or nearly still picture costs what it needs (CBR spent the whole budget on each) */
    c->rc_max_rate = (int64_t)bitrate * 1000;
    c->rc_buffer_size = (int)(c->rc_max_rate / 2);
    /* the RGB input's conversion, NVENC's and swscale's alike: BT.601, limited range */
    c->colorspace = AVCOL_SPC_SMPTE170M;
    c->color_primaries = AVCOL_PRI_BT709;
    c->color_trc = AVCOL_TRC_IEC61966_2_1;
    c->color_range = AVCOL_RANGE_MPEG;
    enum AVPixelFormat input = AV_PIX_FMT_YUV420P;
    switch (which) {
    case ENC_NVENC:
        c->pix_fmt = input = rgb; /* converted on the GPU */
        av_opt_set(c->priv_data, "preset", "p1", 0);
        av_opt_set(c->priv_data, "tune", "ll", 0);
        av_opt_set(c->priv_data, "rc", "vbr", 0);
        av_opt_set_int(c->priv_data, "cq", QUALITY, 0);
        av_opt_set(c->priv_data, "profile", "main", 0);
        av_opt_set_int(c->priv_data, "zerolatency", 1, 0);
        av_opt_set_int(c->priv_data, "delay", 0, 0);
        av_opt_set_int(c->priv_data, "forced-idr", 1, 0);
        break;
    case ENC_VAAPI:
        c->pix_fmt = AV_PIX_FMT_VAAPI;
        c->hw_frames_ctx = av_buffer_ref(s->hwframes);
        input = AV_PIX_FMT_NV12;
        c->bit_rate = c->rc_max_rate / 2;
        av_opt_set(c->priv_data, "rc_mode", "VBR", 0);
        av_opt_set(c->priv_data, "profile", "main", 0);
        av_opt_set_int(c->priv_data, "async_depth", 1, 0);
        break;
    default:
        /* x264 keeps its chroma interleaved, as NV12 has it: taken without a conversion */
        c->pix_fmt = input = gpu ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
        c->thread_count = 0;
        /* libx264 164's sliced threads (zerolatency's) code slices after the first wrong under
         * VBV at a QP below 10 (a flat picture at 20 Mbit/s came out grey): never that low */
        c->qmin = 10;
        av_opt_set(c->priv_data, "preset", "ultrafast", 0);
        av_opt_set(c->priv_data, "tune", "zerolatency", 0);
        av_opt_set(c->priv_data, "profile", "baseline", 0);
        av_opt_set_int(c->priv_data, "crf", QUALITY, 0);
        av_opt_set_int(c->priv_data, "forced-idr", 1, 0);
        break;
    }
    if ((err = avcodec_open2(c, codec, NULL)) < 0) {
        fprintf(stderr, "beanjar-vnc: video: %s: %s\n", enc_codecs[which], av_error(err));
        h264_close(s);
        return err;
    }
    s->frame = av_frame_alloc();
    s->pkt = av_packet_alloc();
    /* swscale's frame and context: the RGB frames' conversion (all of them without the GPU's;
     * with it, those handed on before the encoder opened, or after a conversion failed) */
    if (input != rgb) {
        s->sw = av_frame_alloc();
        s->sw->format = input;
        s->sw->width = w;
        s->sw->height = h;
        s->sws = sws_getContext(w, h, rgb, w, h, input, SWS_BILINEAR, NULL, NULL, NULL);
        if (!s->sws || av_frame_get_buffer(s->sw, 0) < 0) {
            fprintf(stderr, "beanjar-vnc: video: no RGB to YUV conversion\n");
            h264_close(s);
            return AVERROR(ENOMEM);
        }
    }
    s->open = bitrate;
    s->opens++;
    g_atomic_int_set(&s->takes_nv12, gpu);
    fprintf(stderr, "beanjar-vnc: video encoder %s (%s), %dx%d, up to %d fps, %d kbit/s, %s\n",
            enc_names[which], enc_codecs[which], w, h, s->s->fps, bitrate,
            which == ENC_NVENC ? "RGB to YUV in NVENC"
            : gpu              ? "RGB to NV12 on the GPU"
                               : "RGB to YUV by swscale");
    return 0;
}

/* The encoder for bitrate param (reopened for another): the option's, or auto's first that opens
 * (then kept). */
static int encoder_open(struct stream *s, int param)
{
    if ((s->ctx || s->v4l2) && s->open == param)
        return 0;
    h264_close(s);
    if (s->chosen >= 0)
        return h264_try(s, s->chosen, param);
    for (int i = 0; i < ENC_COUNT; i++)
        if (!h264_try(s, i, param)) {
            s->chosen = i;
            return 0;
        }
    return -1;
}

static void pend_keep(struct client *c, const unsigned char *a, size_t na, const unsigned char *b,
                      size_t nb)
{
    size_t n = na + nb;
    if (n > c->pcap) {
        free(c->pend);
        c->pcap = n + n / 2;
        c->pend = malloc(c->pcap);
    }
    memcpy(c->pend, a, na);
    memcpy(c->pend + na, b, nb);
    c->plen = n;
    c->poff = 0;
}

static void flush(struct client *c)
{
    if (!c->plen || c->dead)
        return;
    ssize_t r = send(c->fd, c->pend + c->poff, c->plen - c->poff, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r < 0 && errno != EAGAIN && errno != EINTR) {
        c->dead = 1;
        return;
    }
    if (r > 0) {
        c->poff += (size_t)r;
        c->since = 0;
    }
    if (c->poff == c->plen)
        c->plen = c->poff = 0;
}

/* A unit c had no room for: none until a keyframe, forced once it reads again. */
static void dropped(struct client *c, gint64 now)
{
    c->st->dropped++;
    c->wait_key = c->starved = 1;
    if (!c->since)
        c->since = now;
}

/* One unit to c, whole or not at all (a short write keeps its rest for the next round). */
static void send_unit(struct client *c, const unsigned char *head, const unsigned char *data,
                      size_t n, int key, gint64 now)
{
    if (c->dead || (c->wait_key && !key))
        return;
    flush(c);
    if (c->dead)
        return;
    if (c->plen) { /* the last one's rest is still queued */
        dropped(c, now);
        return;
    }
    struct iovec iov[2] = {{(void *)head, UNIT_HEAD}, {(void *)data, n}};
    struct msghdr m = {.msg_iov = iov, .msg_iovlen = 2};
    ssize_t r = sendmsg(c->fd, &m, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r < 0 && errno != EAGAIN && errno != EINTR) {
        c->dead = 1;
        return;
    }
    if (r <= 0) {
        dropped(c, now);
        return;
    }
    c->wait_key = c->starved = 0;
    c->since = 0;
    size_t sent = (size_t)r;
    if (sent < UNIT_HEAD)
        pend_keep(c, head + sent, UNIT_HEAD - sent, data, n);
    else if (sent < UNIT_HEAD + n)
        pend_keep(c, data + (sent - UNIT_HEAD), UNIT_HEAD + n - sent, NULL, 0);
}

static void put_u32(unsigned char *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (unsigned char)(v >> (8 * i));
}

static void distribute(struct stream *s, const unsigned char *data, size_t n, int key, uint64_t ts,
                       gint64 now)
{
    unsigned char head[UNIT_HEAD] = {0};
    put_u32(head, (uint32_t)n);
    put_u32(head + 4, key ? FLAG_KEY : 0);
    put_u32(head + 8, (uint32_t)ts);
    put_u32(head + 12, (uint32_t)(ts >> 32));
    s->frames++;
    s->keys += key;
    s->bytes += (unsigned long long)n;
    for (struct client *c = s->clients; c; c = c->next)
        send_unit(c, head, data, n, key, now);
}

/* The work frame on the V4L2 encoder (as a keyframe when forced), its unit sent. */
static void encode_v4l2(struct stream *s, const struct vnc_frame *gf, gint64 now)
{
    gint64 origin = s->s->origin, t = s->work_time;
    if (t <= s->last_encode)
        t = s->last_encode + 1; /* the same frame again: a later time */
    s->last_encode = t;
    int force = s->force;
    if (force) {
        s->last_forced = now;
        s->force_count++;
    }
    s->force = 0;
    const unsigned char *out;
    size_t n;
    int key;
    if (h264v4l2_encode(s->v4l2, gf ? NULL : s->work->data, gf, force, &out, &n, &key) < 0)
        return;
    distribute(s, out, n, key, (uint64_t)(t - origin), now);
}

/* The work frame encoded (as a keyframe when forced), its units sent. */
static void encode(struct stream *s, gint64 now)
{
    if (!s->have_work)
        return;
    gint64 origin = s->s->origin;
    const struct vnc_frame *gf =
        s->s->dmabuf && !s->work_nv12 ? (const struct vnc_frame *)s->work->data : NULL;
    if (s->v4l2) {
        if (!s->work_nv12)
            encode_v4l2(s, gf, now);
        return;
    }
    if (!s->ctx || (s->work_nv12 && !s->sw))
        return; /* NV12 for an encoder that takes RGB: the next frame is RGB */
    /* the RGB: RGBX bytes, or a GBM frame's mapping (B, G, R, X; read once, write-combined) */
    const uint8_t *rgb = gf ? gf->pixels : s->work->data;
    int rgb_stride = gf ? (int)gf->stride : s->s->width * 4;
    if (!rgb)
        return;
    int w = s->ctx->width, h = s->ctx->height, err;
    AVFrame *f = s->frame, *hw = NULL;
    av_frame_unref(f);
    if (s->work_nv12) { /* converted on the GPU: referenced (x264 copies it, VA-API uploads it) */
        int stride = s->s->nv12_stride;
        f->format = AV_PIX_FMT_NV12;
        f->width = w;
        f->height = h;
        f->buf[0] = av_buffer_ref(s->work);
        if (!f->buf[0])
            return;
        f->data[0] = s->work->data;
        f->data[1] = s->work->data + (size_t)stride * h;
        f->linesize[0] = f->linesize[1] = stride;
        if (s->hwframes) {
            hw = av_frame_alloc();
            if ((err = av_hwframe_get_buffer(s->hwframes, hw, 0)) < 0 ||
                (err = av_hwframe_transfer_data(hw, f, 0)) < 0) {
                fprintf(stderr, "beanjar-vnc: video: upload: %s\n", av_error(err));
                av_frame_free(&hw);
                av_frame_unref(f);
                return;
            }
            av_frame_unref(f);
            f = hw;
        }
    } else if (!s->sw) { /* NVENC: the RGB as it is, referenced, so libavcodec takes it uncopied */
        f->format = gf ? AV_PIX_FMT_BGR0 : AV_PIX_FMT_RGB0;
        f->width = w;
        f->height = h;
        f->buf[0] = av_buffer_ref(s->work);
        if (!f->buf[0])
            return;
        f->data[0] = (uint8_t *)rgb;
        f->linesize[0] = rgb_stride;
    } else {
        if (av_frame_make_writable(s->sw) < 0)
            return;
        const uint8_t *src[1] = {rgb};
        int stride[1] = {rgb_stride};
        sws_scale(s->sws, src, stride, 0, h, s->sw->data, s->sw->linesize);
        if (s->hwframes) {
            hw = av_frame_alloc();
            if ((err = av_hwframe_get_buffer(s->hwframes, hw, 0)) < 0 ||
                (err = av_hwframe_transfer_data(hw, s->sw, 0)) < 0) {
                fprintf(stderr, "beanjar-vnc: video: upload: %s\n", av_error(err));
                av_frame_free(&hw);
                return;
            }
            f = hw;
        } else
            f = s->sw;
    }
    int64_t pts = s->work_time - origin;
    if (pts <= s->last_encode - origin)
        pts = s->last_encode - origin + 1; /* the same frame again: a later time */
    f->pts = pts;
    f->pict_type = s->force ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
#if LIBAVUTIL_VERSION_MAJOR >= 58
    if (s->force)
        f->flags |= AV_FRAME_FLAG_KEY;
    else
        f->flags &= ~AV_FRAME_FLAG_KEY;
#else
    f->key_frame = s->force;
#endif
    if (s->force) {
        s->last_forced = now;
        s->force_count++;
    }
    s->force = 0;
    s->last_encode = origin + pts;
    err = avcodec_send_frame(s->ctx, f);
    if (hw)
        av_frame_free(&hw);
    else if (f == s->frame)
        av_frame_unref(f);
    if (err < 0) {
        fprintf(stderr, "beanjar-vnc: video: encoding: %s\n", av_error(err));
        return;
    }
    while ((err = avcodec_receive_packet(s->ctx, s->pkt)) >= 0) {
        distribute(s, s->pkt->data, (size_t)s->pkt->size, !!(s->pkt->flags & AV_PKT_FLAG_KEY),
                   (uint64_t)s->pkt->pts, now);
        av_packet_unref(s->pkt);
    }
}

/* The coded size (even) and the bitrate. */
static int send_header(struct stream *s, struct client *c)
{
    unsigned char h[HEADER] = {'B', 'J', 'V', 'I'};
    int w = s->ctx ? s->ctx->width : s->s->width, ht = s->ctx ? s->ctx->height : s->s->height;
    if (s->v4l2) { /* the frame's own size: the encoder crops in its SPS */
        w = s->s->width;
        ht = s->s->height;
    }
    put_u32(h + 4, HEADER);
    h[8] = (unsigned char)w;
    h[9] = (unsigned char)(w >> 8);
    h[10] = (unsigned char)ht;
    h[11] = (unsigned char)(ht >> 8);
    h[12] = (unsigned char)s->s->fps;
    h[13] = (unsigned char)(s->s->fps >> 8); /* a u16: a panel may be above 255 Hz */
    h[14] = CODEC_H264;
    put_u32(h + 16, (uint32_t)s->open);
    strncpy((char *)h + 20, encoder_name(s), 11);
    return send(c->fd, h, sizeof h, MSG_DONTWAIT | MSG_NOSIGNAL) == (ssize_t)sizeof h;
}

static gboolean wake_main(gpointer data)
{
    struct video_server *s = data;
    s->wake(s->data);
    return G_SOURCE_REMOVE;
}

static void reap(struct stream *s)
{
    for (struct client **p = &s->clients; *p;) {
        struct client *c = *p;
        if (!c->dead) {
            p = &c->next;
            continue;
        }
        *p = c->next;
        close(c->fd);
        free(c->pend);
        free(c);
        if (g_atomic_int_dec_and_test(&s->active))
            g_idle_add(wake_main, s->s); /* the last one: beanjar-vnc may idle */
    }
}

/* Whether c is to be polled for room: the rest of a unit to send, or a keyframe to force for it
 * once it reads again. */
static int want_room(struct stream *s, struct client *c, gint64 now)
{
    return c->plen || (c->starved && now - s->last_forced > FORCE_GAP_US);
}

static void *thread_main(void *data)
{
    struct stream *s = data;
    struct pollfd fds[1 + MAX_CLIENTS];
    struct client *polled[MAX_CLIENTS];
    for (;;) {
        gint64 now = g_get_monotonic_time();
        int n = 0, timeout = -1;
        fds[0] = (struct pollfd){.fd = s->efd, .events = POLLIN};
        for (struct client *c = s->clients; c && n < MAX_CLIENTS; c = c->next) {
            polled[n] = c;
            fds[1 + n++] = (struct pollfd){.fd = c->fd,
                                           .events = POLLIN | (want_room(s, c, now) ? POLLOUT : 0)};
        }
        if (s->clients && s->have_work) {
            gint64 due = s->last_encode + IDLE_US - now;
            timeout = due > 0 ? (int)((due + 999) / 1000) : 0;
        }
        for (struct client *c = s->clients; c; c = c->next)
            if (c->since || c->starved || c->wait_key) /* the slow ones' clock, the gap's end */
                timeout = timeout < 0 || timeout > 50 ? 50 : timeout;
        poll(fds, (nfds_t)(1 + n), timeout);
        now = g_get_monotonic_time();
        if (fds[0].revents & POLLIN) {
            uint64_t v;
            if (read(s->efd, &v, sizeof v) < 0 && errno != EAGAIN)
                break;
        }
        for (int i = 0; i < n; i++) {
            struct client *c = polled[i];
            if (fds[1 + i].revents & (POLLIN | POLLHUP | POLLERR)) {
                char junk[256];
                ssize_t r = recv(c->fd, junk, sizeof junk, MSG_DONTWAIT);
                if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR))
                    c->dead = 1;
            }
            if (fds[1 + i].revents & POLLOUT) {
                if (c->plen)
                    flush(c);
                else if (c->starved)
                    c->ready = 1;
            }
        }
        pthread_mutex_lock(&s->lock);
        int stop = s->stop, fresh = 0;
        struct client *incoming = s->incoming;
        s->incoming = NULL;
        AVBufferRef *old = NULL;
        if (s->slot_full) { /* the slot's buffer is the work's now; the next frame takes another */
            old = s->work;
            s->work = s->slot;
            s->work_nv12 = s->slot_nv12;
            s->slot = NULL;
            s->work_time = s->slot_time;
            s->slot_full = 0;
            s->have_work = fresh = 1;
        }
        pthread_mutex_unlock(&s->lock);
        av_buffer_unref(&old); /* back to the pool, unless libavcodec still holds it */
        if (stop) {
            while (incoming) {
                struct client *c = incoming;
                incoming = c->next;
                c->next = s->clients;
                s->clients = c;
            }
            break;
        }
        while (incoming) {
            struct client *c = incoming;
            incoming = c->next;
            int opened = encoder_open(s, c->param) == 0;
            if (!opened || !send_header(s, c)) {
                fprintf(stderr, "beanjar-vnc: video client refused: %s\n",
                        opened ? "the header could not be sent" : "no encoder opens");
                s->refused++;
                close(c->fd);
                free(c);
                if (g_atomic_int_dec_and_test(&s->active))
                    g_idle_add(wake_main, s->s);
                continue;
            }
            /* its stream starts with a keyframe, forced now (a reopened encoder, at another
             * bitrate, starts with one for everyone) */
            c->wait_key = 1;
            c->next = s->clients;
            s->clients = c;
            s->force = 1;
            s->served++;
            /* the frame for it was handed on before the encoder opened, as RGB: one as NV12 */
            if (g_atomic_int_get(&s->takes_nv12) && !s->work_nv12)
                g_idle_add(wake_main, s->s);
        }
        /* the stalled clients gone; a keyframe for a starved one that reads again */
        for (struct client *c = s->clients; c; c = c->next) {
            if (c->since && now - c->since > SLOW_US) {
                s->slow++;
                c->dead = 1;
                fprintf(stderr, "beanjar-vnc: video client disconnected: took nothing for %d s\n",
                        SLOW_US / 1000000);
            } else if (c->ready && !c->dead) {
                s->force = 1;
            }
            c->ready = 0;
        }
        reap(s);
        if (!s->clients) {
            if (s->open) {
                h264_close(s);
                fprintf(stderr, "beanjar-vnc: video encoder closed: no clients\n");
            }
            continue;
        }
        if (s->have_work && (fresh || s->force || now - s->last_encode >= IDLE_US)) {
            if (!fresh)
                s->work_time = now; /* the same picture, again */
            encode(s, now);
            if (fresh) { /* a new picture's time, for the rate's measure (rate.h) */
                __atomic_add_fetch(&s->fresh_us, (unsigned long long)(g_get_monotonic_time() - now),
                                   __ATOMIC_RELAXED);
                __atomic_add_fetch(&s->fresh_n, 1, __ATOMIC_RELAXED);
            }
            reap(s);
        }
    }
    return NULL;
}

/* -- requests (main thread) -- */

static void unlink_requesting(struct client *c)
{
    for (struct client **p = &c->s->requesting; *p; p = &(*p)->next)
        if (*p == c) {
            *p = c->next;
            return;
        }
}

static void refuse(struct client *c, const char *why)
{
    fprintf(stderr, "beanjar-vnc: video client refused: %s\n", why);
    c->s->refused++;
    unlink_requesting(c);
    client_free(c);
}

static gboolean request_timeout(gpointer data)
{
    struct client *c = data;
    c->timeout = 0;
    refuse(c, "no request line");
    return G_SOURCE_REMOVE;
}

/* "h264", then words "bitrate=N": 0 with *bitrate, or -1. */
static int parse_request(char *line, int *bitrate)
{
    char *save = NULL, *word = strtok_r(line, " \t", &save);
    if (!word || strcmp(word, "h264"))
        return -1;
    *bitrate = BITRATE_DEFAULT;
    while ((word = strtok_r(NULL, " \t", &save))) {
        char tail;
        if (sscanf(word, "bitrate=%d%c", bitrate, &tail) != 1 || *bitrate < BITRATE_MIN ||
            *bitrate > BITRATE_MAX)
            return -1;
    }
    return 0;
}

static gboolean request(gint fd, GIOCondition cond, gpointer data)
{
    (void)cond;
    struct client *c = data;
    ssize_t r = recv(fd, c->req + c->reqlen, sizeof c->req - 1 - c->reqlen, MSG_DONTWAIT);
    if (r < 0 && (errno == EAGAIN || errno == EINTR))
        return G_SOURCE_CONTINUE;
    if (r > 0) {
        c->reqlen += (unsigned)r;
        c->req[c->reqlen] = 0;
    }
    char *end = r > 0 ? strchr(c->req, '\n') : NULL;
    if (r > 0 && !end && c->reqlen < sizeof c->req - 1)
        return G_SOURCE_CONTINUE;
    c->watch = 0; /* this source ends with this call */
    if (!end) {
        refuse(c, r > 0 ? "a request longer than a line" : "closed before its request");
        return G_SOURCE_REMOVE;
    }
    *end = 0;
    g_strstrip(c->req);
    if (parse_request(c->req, &c->param) < 0) {
        refuse(c, "not 'h264' with bitrate=N (kbit/s, 100 to 50000)");
        return G_SOURCE_REMOVE;
    }
    if (c->timeout)
        g_source_remove(c->timeout);
    c->timeout = 0;
    struct video_server *s = c->s;
    struct stream *st = c->st = &s->stream;
    if (g_atomic_int_get(&st->active) >= MAX_CLIENTS) {
        refuse(c, "too many video clients");
        return G_SOURCE_REMOVE;
    }
    unlink_requesting(c);
    g_atomic_int_inc(&st->active);
    pthread_mutex_lock(&st->lock);
    c->next = st->incoming;
    st->incoming = c;
    pthread_mutex_unlock(&st->lock);
    uint64_t one = 1;
    if (write(st->efd, &one, sizeof one) < 0)
        perror("beanjar-vnc: video: eventfd");
    s->wake(s->data); /* a fresh frame for it */
    return G_SOURCE_REMOVE;
}

static gboolean accept_ready(gint fd, GIOCondition cond, gpointer data)
{
    (void)cond;
    struct video_server *s = data;
    int cfd = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (cfd < 0)
        return G_SOURCE_CONTINUE;
    int size = SNDBUF;
    setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
    struct client *c = calloc(1, sizeof *c);
    c->s = s;
    c->fd = cfd;
    c->next = s->requesting;
    s->requesting = c;
    c->watch = g_unix_fd_add(cfd, G_IO_IN | G_IO_HUP | G_IO_ERR, request, c);
    c->timeout = g_timeout_add(REQUEST_MS, request_timeout, c);
    return G_SOURCE_CONTINUE;
}

static void stream_stop(struct stream *st)
{
    pthread_mutex_lock(&st->lock);
    st->stop = 1;
    pthread_mutex_unlock(&st->lock);
    uint64_t one = 1;
    if (write(st->efd, &one, sizeof one) < 0)
        perror("beanjar-vnc: video: eventfd");
    pthread_join(st->thread, NULL);
}

struct video_server *video_server_new(const char *path, const char *encoder, int width, int height,
                                      int fps, const struct vnc_frame *gbm, int nv12,
                                      void (*wake)(void *), void *data)
{
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof addr.sun_path) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(addr.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return NULL;
    unlink(path);              /* a previous run's */
    mode_t mask = umask(0177); /* owner-only from its bind on */
    int r = bind(fd, (struct sockaddr *)&addr, sizeof addr);
    umask(mask);
    if (r < 0 || listen(fd, 8) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return NULL;
    }
    struct video_server *s = calloc(1, sizeof *s);
    s->path = strdup(path);
    s->fd = fd;
    s->width = width;
    s->height = height;
    s->fps = fps;
    if (gbm) {
        s->gbm = *gbm;
        s->dmabuf = gbm->stride;
    }
    s->wake = wake;
    s->data = data;
    s->origin = g_get_monotonic_time();
    av_log_set_level(AV_LOG_ERROR);
    struct stream *st = &s->stream;
    st->s = s;
    st->chosen = named(enc_names, ENC_COUNT, encoder);
    st->efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    st->pool = av_buffer_pool_init((size_t)width * height * 4, NULL);
    s->nv12_w = width & ~1;
    s->nv12_h = height & ~1;
    s->nv12_stride = yuv_stride(s->nv12_w);
    g_atomic_int_set(&s->nv12, nv12 && s->nv12_w >= 2 && s->nv12_h >= 2);
    st->nv12_pool = av_buffer_pool_init((size_t)s->nv12_stride * s->nv12_h * 3 / 2, NULL);
    st->last_encode = s->origin;
    pthread_mutex_init(&st->lock, NULL);
    if (pthread_create(&st->thread, NULL, thread_main, st)) {
        int e = errno;
        close(st->efd);
        av_buffer_pool_uninit(&st->pool);
        av_buffer_pool_uninit(&st->nv12_pool);
        close(fd);
        unlink(path);
        free(s->path);
        free(s);
        errno = e;
        return NULL;
    }
    s->watch = g_unix_fd_add(fd, G_IO_IN, accept_ready, s);
    return s;
}

int video_server_wanted(struct video_server *s)
{
    return s && g_atomic_int_get(&s->stream.active) > 0;
}

void video_server_frame(struct video_server *s, const void *rgbx)
{
    if (!s)
        return;
    struct stream *st = &s->stream;
    if (g_atomic_int_get(&st->active) <= 0)
        return;
    pthread_mutex_lock(&st->lock);
    if (!st->slot && !(st->slot = av_buffer_pool_get(st->pool))) {
        pthread_mutex_unlock(&st->lock);
        return;
    }
    if (st->slot_nv12) { /* an NV12 one not taken yet: replaced */
        av_buffer_unref(&st->slot);
        st->slot_nv12 = 0;
        if (!(st->slot = av_buffer_pool_get(st->pool))) {
            pthread_mutex_unlock(&st->lock);
            return;
        }
    }
    memcpy(st->slot->data, rgbx, (size_t)s->width * s->height * 4);
    st->slot_time = g_get_monotonic_time();
    st->slot_full = 1;
    pthread_mutex_unlock(&st->lock);
    uint64_t one = 1;
    if (write(st->efd, &one, sizeof one) < 0)
        perror("beanjar-vnc: video: eventfd");
}

static void frame_buffer_free(void *opaque, uint8_t *data)
{
    (void)opaque;
    vnc_frame_unref((struct vnc_frame *)data);
}

void video_server_gbm_frame(struct video_server *s, struct vnc_frame *f)
{
    if (!s)
        return;
    struct stream *st = &s->stream;
    if (g_atomic_int_get(&st->active) <= 0)
        return;
    vnc_frame_ref(f);
    AVBufferRef *b =
        av_buffer_create((uint8_t *)f, sizeof *f, frame_buffer_free, NULL, AV_BUFFER_FLAG_READONLY);
    if (!b) {
        vnc_frame_unref(f);
        return;
    }
    pthread_mutex_lock(&st->lock);
    AVBufferRef *old = st->slot; /* not taken yet: replaced by the newer one */
    st->slot = b;
    st->slot_nv12 = 0;
    st->slot_time = g_get_monotonic_time();
    st->slot_full = 1;
    pthread_mutex_unlock(&st->lock);
    av_buffer_unref(&old);
    uint64_t one = 1;
    if (write(st->efd, &one, sizeof one) < 0)
        perror("beanjar-vnc: video: eventfd");
}

int video_server_rgb_wanted(struct video_server *s)
{
    if (!s || g_atomic_int_get(&s->stream.active) <= 0)
        return 0;
    return !g_atomic_int_get(&s->nv12) || !g_atomic_int_get(&s->stream.takes_nv12);
}

int video_server_nv12_get(struct video_server *s, struct video_nv12 *p)
{
    if (!s || !g_atomic_int_get(&s->nv12))
        return 0;
    struct stream *st = &s->stream;
    if (g_atomic_int_get(&st->active) <= 0 || !g_atomic_int_get(&st->takes_nv12))
        return 0;
    AVBufferRef *b = av_buffer_pool_get(st->nv12_pool);
    if (!b)
        return 0;
    *p = (struct video_nv12){.data = b->data,
                             .width = s->nv12_w,
                             .height = s->nv12_h,
                             .stride = s->nv12_stride,
                             .ref = b};
    return 1;
}

void video_server_nv12_put(struct video_server *s, struct video_nv12 *p, int filled)
{
    AVBufferRef *b = p->ref;
    p->ref = NULL;
    if (!filled) {
        av_buffer_unref(&b);
        g_atomic_int_set(&s->nv12, 0);
        return;
    }
    struct stream *st = &s->stream;
    pthread_mutex_lock(&st->lock);
    AVBufferRef *old = st->slot; /* not taken yet: replaced by the newer one */
    st->slot = b;
    st->slot_nv12 = 1;
    st->slot_time = g_get_monotonic_time();
    st->slot_full = 1;
    pthread_mutex_unlock(&st->lock);
    av_buffer_unref(&old);
    uint64_t one = 1;
    if (write(st->efd, &one, sizeof one) < 0)
        perror("beanjar-vnc: video: eventfd");
}

void video_server_encoded(struct video_server *s, unsigned long long *frames,
                          unsigned long long *us)
{
    *frames = s ? __atomic_load_n(&s->stream.fresh_n, __ATOMIC_RELAXED) : 0;
    *us = s ? __atomic_load_n(&s->stream.fresh_us, __ATOMIC_RELAXED) : 0;
}

void video_server_set_fps(struct video_server *s, int fps)
{
    if (s)
        __atomic_store_n(&s->fps, fps, __ATOMIC_RELAXED);
}

unsigned long long video_server_units(struct video_server *s)
{
    return s ? __atomic_load_n(&s->stream.frames, __ATOMIC_RELAXED) : 0;
}

void video_server_free(struct video_server *s)
{
    if (!s)
        return;
    g_source_remove(s->watch);
    close(s->fd);
    unlink(s->path);
    while (s->requesting) {
        struct client *c = s->requesting;
        s->requesting = c->next;
        client_free(c);
    }
    struct stream *h = &s->stream;
    stream_stop(h);
    while (h->clients) {
        struct client *c = h->clients;
        h->clients = c->next;
        close(c->fd);
        free(c->pend);
        free(c);
    }
    h264_close(h);
    fprintf(stderr,
            "beanjar-vnc: video clients=%llu refused=%llu encoder=%s opens=%llu frames=%llu "
            "keyframes=%llu forced=%llu bytes=%llu dropped=%llu slow_disconnects=%llu\n",
            h->served, s->refused + h->refused, encoder_name(h), h->opens, h->frames, h->keys,
            h->force_count, h->bytes, h->dropped, h->slow);
    close(h->efd);
    pthread_mutex_destroy(&h->lock);
    av_buffer_unref(&h->slot);
    av_buffer_unref(&h->work);
    av_buffer_pool_uninit(&h->pool);
    av_buffer_pool_uninit(&h->nv12_pool);
    free(s->path);
    free(s);
}
