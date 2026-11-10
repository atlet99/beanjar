// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* "omx_lxvideosink" and "lxvideosink": W17H's GstLxVideoSink (libgstlxvideosink-1.0.so) for the
 * emulator.
 *
 * The stock sink takes omx_lxvideodec's I420 frames and writes them to LG1313's video output
 * (liblxv, /dev/vo); it is gstcool.conf's default video sink, which playbin3's playsink makes,
 * and libpf's MSE pipeline links it after the decoder and a queue. Here the "frames" are the
 * compressed stream, passed through by our omx_lxvideodec under raw caps that name the stream's
 * own (esdec.c), and this sink sends them to the host (essink.c, as h13's vdecsink does).
 *
 * Stream info: codec, size, frame rate, pixel aspect ratio and scan type from the stream's caps
 * (or its H.264 SPS: streaminfo.c), posted as LG's "media-info" and "video-info" messages, which
 * the pipeline needs before it reports the video loaded. Its VDEC channel is the video port of
 * the resource-info libpf sets, which is also the port tvservice connects the scaler window to
 * (HAL_VSC_Connect, guest/layers/soc/src/libhal_lg115x): the host matches the two. */
#include <gst/gst.h>
#include <string.h>
#include <syslog.h>
#include "props.h"
#include "essink.h"

GST_DEBUG_CATEGORY_STATIC(lxvideosink_debug);
#define GST_CAT_DEFAULT lxvideosink_debug

/* The stock template */
#define SINK_CAPS                                                                                  \
    "video/x-raw, format = (string) I420, width = (int) [ 16, 4096 ], "                            \
    "height = (int) [ 16, 4096 ], framerate = (fraction) [ 0/1, 2147483647/1 ]"

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(SINK_CAPS));

/* 3D_Type 11 is "2d" in libplayerAPIs' table. */
#define VIDEO_2D 11u

#define R G_PARAM_READABLE
#define W G_PARAM_WRITABLE
/* As gst-inspect-1.0 lists GstLxVideoSink's own (W17H, under qemu-user), after GstBaseSink's. */
#ifdef O22N2_MEDIA
#include "o22n2-lxvideosink-props.h"
#else
enum {
    P_LOW_DELAY = 1,
    P_THUMBNAIL_MODE,
    P_LIPSYNC_OFFSET,
    P_WINDOW_ID,
    P_LOW_LATENCY_TEMP,
    P_VSINK_ASYNC_HANDLING,
    P_VSINK_SYNC_ON_CLOCK,
    P_CONSTANT_DELAY,
    P_RESOURCE_INFO,
    P_INTERLEAVING_TYPE,
    P_CURRENT_PTS,
    P_APP_TYPE,
    P_SVP_VERSION,
    P_DISPLAYED_FRAMES,
    P_DROPPED_FRAMES,
    P_ATSC3_MODE,
    P_DISPLAY_PREROLL,
    P_CLOCK_SOURCE,
    N_PROPS = P_CLOCK_SOURCE
};
static const struct prop_def props[N_PROPS] = {
    {"low-delay", "VDEC low delay", G_TYPE_UINT, 0},
    {"thumbnail-mode", "video thumbnail decoding mode toggle property", G_TYPE_BOOLEAN, 0},
    {"lipsync-offset", "lipsync offset(msec) of video", G_TYPE_UINT, 400},
    {"window-id", "DE window ID", G_TYPE_INT, 0},
    {"low-latency-temp", "low latency flag", G_TYPE_INT, 0},
    {"vsink-async-handling", "Asynchoronous state changes mode", G_TYPE_BOOLEAN, 0},
    {"vsink-sync-on-clock", "synchoronize on the clock mode", G_TYPE_BOOLEAN, 0},
    {"constant-delay", "constant delay size in Mirracast", G_TYPE_UINT, 0},
    {"resource-info", "resource information about core-type, video-port, audio-port",
     PROP_STRUCTURE, 0, NULL, W},
    {"interleaving-type", "support vudu 3D", G_TYPE_INT, 0},
    {"current-pts", "Get current PTS", G_TYPE_UINT64, 0, NULL, R},
    {"app-type", "Set application type", G_TYPE_STRING, 0},
    {"svp-version",
     "Secure Video Path. 0 for normal play, 10 for WebOS 1.0 SVP, 20 for WebOS 1.5 SVP.",
     G_TYPE_UINT, 0},
    {"displayed-frames", "Get number of displayed frames from last check for Amazon", G_TYPE_UINT,
     0, NULL, R},
    {"dropped-frames", "Get number of dropped frames from last check for Amazon", G_TYPE_UINT, 0,
     NULL, R},
    {"atsc3-mode", "Work for ATSC3.0 rendering", G_TYPE_BOOLEAN, 0},
    {"display-preroll", "Display preroll frame", G_TYPE_BOOLEAN, 1},
    {"clock-source", "Clock Source", G_TYPE_STRING, 0, "default"},
};
#endif

typedef struct {
    EsSink parent;
    GValue values[N_PROPS];
} GstLxVideoSink;
typedef struct {
    EsSinkClass parent_class;
    void (*video_underrun)(GstLxVideoSink *self);
} GstLxVideoSinkClass;

static GType gst_lx_video_sink_get_type(void);
G_DEFINE_TYPE(GstLxVideoSink, gst_lx_video_sink, es_sink_get_type())

/* LG's two messages, as the stock sink's media_info_new and video_info_new build them (field names
 * and GTypes from its argument lists), posted as application messages. libpf reads "media-info"
 * (Pipeline::handleBusApplication): "type" 1 sends it to handleGstMediaInfoVideo, which keeps the
 * stream's track info by its stream-id and reads width and height as int; nothing in W17H's
 * pipeline reads "video-info", which is WT1H's message (vdecsink.c). */
static void post_info(EsSink *self)
{
    struct stream_info *i = &self->info;
    gint ch = self->channel > 0 ? self->channel : 0;
    gchar *stream_id = gst_pad_get_stream_id(GST_BASE_SINK_PAD(self));
    GstStructure *media = gst_structure_new(
        "media-info", "stream-id", G_TYPE_STRING, stream_id, "type", G_TYPE_INT, 1, /* video */
        "3D_Type", G_TYPE_UINT, VIDEO_2D, "PAR_Width", G_TYPE_UINT, i->par_n, "PAR_Height",
        G_TYPE_UINT, i->par_d, "LR_Type", G_TYPE_INT, 0, "framerate", GST_TYPE_FRACTION, i->fps_n,
        i->fps_n > 0 ? i->fps_d : 1, "afd", G_TYPE_UINT, 0u, NULL);
    gst_structure_set(
        media, "vdec-ch", G_TYPE_INT, ch, "mime-type", G_TYPE_STRING, self->mime, "width",
        G_TYPE_INT, (gint)i->width, "height", G_TYPE_INT, (gint)i->height, "Scan_Type", G_TYPE_UINT,
        (guint)i->scan_type, "crop_left", G_TYPE_UINT, i->crop_left, "crop_rigth", G_TYPE_UINT,
        i->crop_right, /* sic, as LG spells it */
        "crop_top", G_TYPE_UINT, i->crop_top, "crop_bottom", G_TYPE_UINT, i->crop_bottom, NULL);
    GstStructure *info = gst_structure_new(
        "video-info", "vdec-ch", G_TYPE_UINT, (guint)ch, "codec", G_TYPE_STRING, i->codec, "width",
        G_TYPE_UINT, i->width, "height", G_TYPE_UINT, i->height, "framerate", GST_TYPE_FRACTION,
        i->fps_n, i->fps_n > 0 ? i->fps_d : 1, "3D_Type", G_TYPE_UINT, VIDEO_2D, "PAR_Width",
        G_TYPE_UINT, i->par_n, "PAR_Height", G_TYPE_UINT, i->par_d, "Scan_Type", G_TYPE_INT,
        i->scan_type, "LR_Type", G_TYPE_INT, 0, "crop_left", G_TYPE_UINT, i->crop_left,
        "crop_rigth", G_TYPE_UINT, i->crop_right, "crop_top", G_TYPE_UINT, i->crop_top,
        "crop_bottom", G_TYPE_UINT, i->crop_bottom, NULL);
    syslog(LOG_INFO,
           "lxvideosink: media-info vdec-ch=%d %s (%s) %ux%u framerate=%d/%d par=%u:%u scan=%d "
           "stream-id=%s",
           ch, self->mime, i->codec, i->width, i->height, i->fps_n, i->fps_d, i->par_n, i->par_d,
           i->scan_type, stream_id ? stream_id : "-");
    g_free(stream_id);
    gst_element_post_message(GST_ELEMENT(self),
                             gst_message_new_application(GST_OBJECT(self), media));
    gst_element_post_message(GST_ELEMENT(self),
                             gst_message_new_application(GST_OBJECT(self), info));
}

static void gst_lx_video_sink_set_property(GObject *obj, guint id, const GValue *value,
                                           GParamSpec *spec)
{
    GstLxVideoSink *self = (GstLxVideoSink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(value, &self->values[id - 1]);
    GST_OBJECT_UNLOCK(self);
    if (id == P_RESOURCE_INFO)
        es_sink_resource_info(ES_SINK(self), value, "video-port");
}

static void gst_lx_video_sink_get_property(GObject *obj, guint id, GValue *value, GParamSpec *spec)
{
    GstLxVideoSink *self = (GstLxVideoSink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    if (id == P_CURRENT_PTS) {
        /* the stream time being shown: the host shows the frame due on the pipeline clock */
        g_value_set_uint64(value, es_sink_position(ES_SINK(self)));
        return;
    }
    GST_OBJECT_LOCK(self);
    switch (id) {
    case P_DISPLAYED_FRAMES:
        g_value_set_uint(value, ES_SINK(self)->rendered);
        break;
    case P_DROPPED_FRAMES:
        g_value_set_uint(value, 0);
        break;
    default:
        g_value_copy(&self->values[id - 1], value);
        break;
    }
    GST_OBJECT_UNLOCK(self);
}

static void gst_lx_video_sink_finalize(GObject *obj)
{
    props_clear(((GstLxVideoSink *)obj)->values, N_PROPS);
    G_OBJECT_CLASS(gst_lx_video_sink_parent_class)->finalize(obj);
}

static void gst_lx_video_sink_class_init(GstLxVideoSinkClass *klass)
{
    GObjectClass *gobject = G_OBJECT_CLASS(klass);
    GstElementClass *element = GST_ELEMENT_CLASS(klass);
    gobject->set_property = gst_lx_video_sink_set_property;
    gobject->get_property = gst_lx_video_sink_get_property;
    gobject->finalize = gst_lx_video_sink_finalize;
    props_install(gobject, props, N_PROPS);
    klass->parent_class.kind = WEBOS_MEDIA_VIDEO;
    klass->parent_class.post_info = post_info;
    g_signal_new("video-underrun", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstLxVideoSinkClass, video_underrun), NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
#ifdef O22N2_MEDIA
    /* Host-decoded buffers are unavailable for guest readback. These action
     * defaults return NULL/FALSE rather than pretending to render/read pixels.
     * W22O ponytail's sink (LX_REL 25) has no last-sample action; queue's adds video-frame-info. */
#if LX_REL < 25
    g_signal_new("last-sample", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0,
                 NULL, NULL, g_cclosure_marshal_generic, GST_TYPE_BUFFER, 1, GST_TYPE_CAPS);
#endif
#if LX_REL >= 26
    g_signal_new("video-frame-info", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                 g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
#endif
    g_signal_new("render-prerolled-frame", G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_generic,
                 G_TYPE_BOOLEAN, 0);
    g_signal_new("clock-calibrated", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
#endif
    gst_element_class_add_pad_template(element, gst_static_pad_template_get(&sink_template));
    gst_element_class_set_static_metadata(
        element, "lxvideo sink", "Sink/Video",
        "LG's video sink for the emulator: sends the stream to the host viewer", "Beanjar");
}

static void gst_lx_video_sink_init(GstLxVideoSink *self)
{
    props_init(self->values, props, N_PROPS);
}

/* The second factory, lxvideosink, as its own type: an element's factory is its type's. */
typedef GstLxVideoSink GstLxVideoSinkPlain;
typedef GstLxVideoSinkClass GstLxVideoSinkPlainClass;
static GType gst_lx_video_sink_plain_get_type(void);
G_DEFINE_TYPE(GstLxVideoSinkPlain, gst_lx_video_sink_plain, gst_lx_video_sink_get_type())
static void gst_lx_video_sink_plain_class_init(GstLxVideoSinkPlainClass *klass)
{
}
static void gst_lx_video_sink_plain_init(GstLxVideoSinkPlain *self)
{
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(lxvideosink_debug, "lxvideosink", 0, "LX video sink (emulator)");
    /* the stock ranks; gstcool.conf makes omx_lxvideosink the default video sink */
    return gst_element_register(plugin, "omx_lxvideosink", GST_RANK_SECONDARY,
                                gst_lx_video_sink_get_type()) &&
           gst_element_register(plugin, "lxvideosink", GST_RANK_PRIMARY,
                                gst_lx_video_sink_plain_get_type());
}

#define PACKAGE "beanjar"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, lxvideosink,
                  "LG's LX video sink for the emulator", plugin_init, "1.0", "LGPL", PACKAGE,
                  "Beanjar")
