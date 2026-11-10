// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* "dvovideosink": Realtek's GstDvoVideoSink (libgstdvo.so, W19P, W20L, W21P) for the emulator.
 *
 * The stock sink takes the OMX decoders' pictures in Realtek's DVO memory (video/x-raw(memory:dvo),
 * NV12, with a GstDvoBufferMeta carrying physical addresses) and hands them to the video CPU's VO
 * through DirectVo; it is gstcool.conf's default video sink, which playbin3's playsink makes. Here
 * the decoders (rtkomx.c) pass the compressed stream through under raw caps of that kind whose
 * webos-es-caps field names the stream's own (esdec.c), and this sink sends it to the host
 * (essink.c), on the channel of resource-info's video port. No DVO pool, meta, VO or reference
 * clock: the private decoder -> sink link carries no hardware state. The scaler window is not the
 * sink's: tvservice (K5) or videooutputd (K6, K7) sets it through the stock HAL and VAL, and
 * rtk.ko passes it on.
 *
 * The pipeline reports the video loaded once the sink has posted LG's "media-info" application
 * message, which libpf's Pipeline::handleBusApplication takes by its "type" (1: video) to the
 * stream's track (by "stream-id"). RTK_REL is the release: 19 (W19P), 20 (W20L), 21 (W21P). */
#include <gst/gst.h>
#include <string.h>
#include <syslog.h>
#include "props.h"
#include "essink.h"

GST_DEBUG_CATEGORY_STATIC(dvo_debug);
#define GST_CAT_DEFAULT dvo_debug

#ifndef RTK_REL
#error "RTK_REL: 19, 20 or 21"
#endif

/* The stock template (gst-inspect-1.0 of the stock libgstdvo.so); W21P's is NV12 in DVO memory only
 */
#if RTK_REL >= 21
#define DVO_SINK_CAPS                                                                              \
    "video/x-raw(memory:dvo), format=(string)NV12, width=(int)[ 1, 2147483647 ], "                 \
    "height=(int)[ 1, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]"
#else
#define DVO_SINK_CAPS                                                                              \
    "video/x-raw(memory:dvo), format=(string)NV12, width=(int)[ 1, 2147483647 ], "                 \
    "height=(int)[ 1, 2147483647 ], framerate=(fraction)[ 0/1, 2147483647/1 ]; "                   \
    "video/x-raw, format=(string)BGRA, width=(int)[ 1, 2147483647 ], height=(int)[ 1, 2147483647 " \
    "], "                                                                                          \
    "framerate=(fraction)[ 0/1, 2147483647/1 ]"
#endif

#if RTK_REL >= 21
#define DVO_KLASS "Sink/Video/Hardware"
#else
#define DVO_KLASS "Sink/Video"
#endif

/* 3D_Type 11 is "2d" in libplayerAPIs' table. */
#define VIDEO_2D 11u

#define R G_PARAM_READABLE
#define W G_PARAM_WRITABLE
#include "rtkdvoprops.h"
#define N_PROPS G_N_ELEMENTS(props)

typedef struct {
    EsSink parent;
    GValue values[N_PROPS];
} GstDvoVideoSink;
typedef struct {
    EsSinkClass parent_class;
    void (*video_underrun)(GstDvoVideoSink *self);
    GstBuffer *(*last_sample)(GstDvoVideoSink *self, GstCaps *caps);
    GstSample *(*convert_sample)(GstDvoVideoSink *self, GstCaps *caps);
    guint64 (*step_next)(GstDvoVideoSink *self);
    void (*render_prerolled_frame)(GstDvoVideoSink *self);
} GstDvoVideoSinkClass;

static GType gst_dvo_video_sink_get_type(void);
G_DEFINE_TYPE(GstDvoVideoSink, gst_dvo_video_sink, es_sink_get_type())

/* The media-info the stock sink posts: the decoder's (libgstomx's builder: W19P 0xf0fc), which it
 * queries from upstream (custom query 0x7803 "media-info", the structure in its "value") and posts
 * as an application message with the sink pad's stream-id. Ours builds it from the stream's caps,
 * with the builder's fields and types; the HDR keys are left out (no HDR metadata is parsed). */
static void post_info(EsSink *self)
{
    struct stream_info *i = &self->info;
    gchar *stream_id = gst_pad_get_stream_id(GST_BASE_SINK_PAD(self));
    GstStructure *media = gst_structure_new(
        "media-info", "stream-id", G_TYPE_STRING, stream_id, "type", G_TYPE_INT, 1, "mime-type",
        G_TYPE_STRING, self->mime, "width", G_TYPE_INT, (gint)i->width, "height", G_TYPE_INT,
        (gint)i->height, "PAR_Width", G_TYPE_UINT, i->par_n, "PAR_Height", G_TYPE_UINT, i->par_d,
        "3D_Type", G_TYPE_UINT, VIDEO_2D, "LR_Type", G_TYPE_INT, 0, "Scan_Type", G_TYPE_UINT,
        (guint)i->scan_type, "afd", G_TYPE_UINT, 0u, "role", G_TYPE_UINT, 0u, "hdr-type",
        G_TYPE_STRING, "none", "framerate", GST_TYPE_FRACTION, i->fps_n,
        i->fps_n > 0 ? i->fps_d : 1, NULL);
    syslog(LOG_INFO,
           "dvovideosink: media-info channel %d %s (%s) %ux%u framerate=%d/%d par=%u:%u scan=%d "
           "stream-id=%s",
           self->channel, self->mime, i->codec, i->width, i->height, i->fps_n, i->fps_d, i->par_n,
           i->par_d, i->scan_type, stream_id ? stream_id : "-");
    g_free(stream_id);
    gst_element_post_message(GST_ELEMENT(self),
                             gst_message_new_application(GST_OBJECT(self), media));
}

static void gst_dvo_video_sink_set_property(GObject *obj, guint id, const GValue *value,
                                            GParamSpec *spec)
{
    GstDvoVideoSink *self = (GstDvoVideoSink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(value, &self->values[id - 1]);
    GST_OBJECT_UNLOCK(self);
    if (!strcmp(spec->name, "resource-info"))
        es_sink_resource_info(ES_SINK(self), value, "video-port");
}

static void gst_dvo_video_sink_get_property(GObject *obj, guint id, GValue *value, GParamSpec *spec)
{
    GstDvoVideoSink *self = (GstDvoVideoSink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    if (!strcmp(spec->name, "current-pts")) {
        g_value_set_uint64(value, es_sink_position(ES_SINK(self)));
        return;
    }
    if (g_str_has_suffix(spec->name, "displayed-frames")) {
        g_value_set_int(value, (gint)ES_SINK(self)->rendered);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(&self->values[id - 1], value);
    GST_OBJECT_UNLOCK(self);
}

/* The pictures are decoded on the host: nothing to hand back. */
static GstBuffer *last_sample(GstDvoVideoSink *self, GstCaps *caps)
{
    GST_INFO_OBJECT(self, "last-sample: the decoded pictures are on the host");
    return NULL;
}

static GstSample *convert_sample(GstDvoVideoSink *self, GstCaps *caps)
{
    GST_INFO_OBJECT(self, "convert-sample: the decoded pictures are on the host");
    return NULL;
}

static guint64 step_next(GstDvoVideoSink *self)
{
    return es_sink_position(ES_SINK(self));
}

static void render_prerolled_frame(GstDvoVideoSink *self)
{
}

static void gst_dvo_video_sink_finalize(GObject *obj)
{
    props_clear(((GstDvoVideoSink *)obj)->values, N_PROPS);
    G_OBJECT_CLASS(gst_dvo_video_sink_parent_class)->finalize(obj);
}

static void gst_dvo_video_sink_class_init(GstDvoVideoSinkClass *klass)
{
    GObjectClass *gobject = G_OBJECT_CLASS(klass);
    GstElementClass *element = GST_ELEMENT_CLASS(klass);
    gobject->set_property = gst_dvo_video_sink_set_property;
    gobject->get_property = gst_dvo_video_sink_get_property;
    gobject->finalize = gst_dvo_video_sink_finalize;
    props_install(gobject, props, N_PROPS);
    klass->parent_class.kind = WEBOS_MEDIA_VIDEO;
    klass->parent_class.post_info = post_info;
    klass->last_sample = last_sample;
    klass->convert_sample = convert_sample;
    klass->step_next = step_next;
    klass->render_prerolled_frame = render_prerolled_frame;
    g_signal_new("video-underrun", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstDvoVideoSinkClass, video_underrun), NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
    g_signal_new("last-sample", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 G_STRUCT_OFFSET(GstDvoVideoSinkClass, last_sample), NULL, NULL,
                 g_cclosure_marshal_generic, GST_TYPE_BUFFER, 1, GST_TYPE_CAPS);
    g_signal_new("convert-sample", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 G_STRUCT_OFFSET(GstDvoVideoSinkClass, convert_sample), NULL, NULL,
                 g_cclosure_marshal_generic, GST_TYPE_SAMPLE, 1, GST_TYPE_CAPS);
    g_signal_new("step-next", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 G_STRUCT_OFFSET(GstDvoVideoSinkClass, step_next), NULL, NULL,
                 g_cclosure_marshal_generic, G_TYPE_UINT64, 0);
    g_signal_new("render-prerolled-frame", G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 G_STRUCT_OFFSET(GstDvoVideoSinkClass, render_prerolled_frame), NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
    gst_element_class_add_pad_template(element,
                                       gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                            gst_caps_from_string(DVO_SINK_CAPS)));
    gst_element_class_set_static_metadata(element, "Realtek video sink", DVO_KLASS,
                                          "Realtek SoC videosink",
                                          "luke.lin <luke.lin@realtek.com>");
}

static void gst_dvo_video_sink_init(GstDvoVideoSink *self)
{
    props_init(self->values, props, N_PROPS);
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(dvo_debug, "dvovideosink", 0, "Realtek video sink (emulator)");
    return gst_element_register(plugin, "dvovideosink", GST_RANK_PRIMARY,
                                gst_dvo_video_sink_get_type());
}

#define PACKAGE "beanjar"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, dvo,
                  "Realtek GStreamer Plugin for the emulator", plugin_init, "1.0.0", "LGPL",
                  PACKAGE, "Beanjar")
