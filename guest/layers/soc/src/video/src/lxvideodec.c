// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* "omx_lxvideodec" and "lxvideodec": W17H's GstLXVideoDec (libgstlxvideodec-1.0.so) for the
 * emulator.
 *
 * The stock element decodes on LG1313's VDEC (liblxv, /dev/vdec) into I420 frames in HMA memory
 * for omx_lxvideosink. playbin3's decodebin3 reaches it through LG's decproxy (gstcool.conf ranks
 * it 290), libpf links it decoder -> queue -> sink for MSE. This one keeps its surface (the
 * factory names and ranks, the pad templates, the properties with their types and flags, the
 * video-underrun signal, the svp-handle and convert-frame actions) and decodes nothing: the
 * compressed buffers go on to the sink unchanged under raw caps that name the stream's own
 * (esdec.c), and omx_lxvideosink (lxvideosink.c) sends them to the host. */
#include <gst/gst.h>
#include <string.h>
#include <syslog.h>
#include "props.h"
#include "esdec.h"
#include "streaminfo.h"

GST_DEBUG_CATEGORY_STATIC(lxvideodec_debug);
#define GST_CAT_DEFAULT lxvideodec_debug

/* The stock templates, verbatim; the modern LX one only with the stock entries, in stock order,
 * of the codecs carried here (W22O..W24G list these, and umediaserver's
 * device_codec_capability_config.json MPEG2, VP9 and AV1; YouTube picks AV1 and VP9 for VOD).
 * W24G's svp-handle action takes a pointer, the earlier ones a guint. */
#ifdef O22N2_MEDIA
#define SINK_CAPS                                                                                  \
    "video/x-h264;video/x-h265,alignment = (string) { au, nal }, "                                 \
    "stream-format = (string) { hvc1, hev1, byte-stream };video/x-hevc;"                           \
    "video/mpeg, systemstream = (boolean)FALSE;video/x-vp8;video/x-vp9;video/x-av1"
#if LX_REL >= 24
#define SVP_HANDLE_POINTER
#endif
#else
#define SINK_CAPS                                                                                  \
    "video/x-divx;video/x-h263;video/x-h264;video/x-h265,alignment = (string) { au, nal }, "       \
    "stream-format = (string) { hvc1, hev1, byte-stream };video/x-hevc;video/x-intel-h263;"        \
    "video/mpeg, systemstream = (boolean)FALSE;video/x-wmv,wmvversion = (int)3;video/x-msmpeg;"    \
    "video/x-pn-realvideo,rmversion = (int) { 3,4 };video/x-svq,svqversion = (int) { 1,3 "         \
    "};video/x-ffv;"                                                                               \
    "video/x-3ivx;video/x-vp8;video/x-vp9;video/x-xvid;video/x-flash-video;video/x-fd;video/"      \
    "x-raw;"
#endif
#define SRC_CAPS                                                                                   \
    "video/x-raw, format = (string) I420, width = (int) [ 16, 4096 ], "                            \
    "height = (int) [ 16, 4096], framerate = (fraction) [ 0/1, 2147483647/1]"

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(SINK_CAPS));
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SRC_CAPS));

#define R G_PARAM_READABLE
#define W G_PARAM_WRITABLE
/* As gst-inspect-1.0 lists GstLXVideoDec's (W17H, under qemu-user). */
#ifdef O22N2_MEDIA
#include "o22n2-lxvideodec-props.h"
#else
enum {
    P_DECODED_SIZE = 1,
    P_CURRENT_PTS,
    P_THUMBNAIL_MODE,
    P_ENLARGE_MAX_LATENESS,
    P_INTERLEAVING_TYPE,
    P_UNDECODED_SIZE,
    P_SVP_VERSION,
    P_APP_TYPE,
    P_STREAM_WIDTH,
    P_STREAM_HEIGHT,
    P_FRAMERATE_NUM,
    P_FRAMERATE_DENOM,
    P_VDEC_CH,
    P_DISH_TRICK,
    P_DISH_TRICK_IGNORE_RATE,
    P_RESOURCE_INFO,
    P_SVP_MEM_SIZE,
    P_SVP_ALIGN_SIZE,
    P_SVP_BASEADDR,
    P_SVP_READ_PTR,
    P_SVP_WRITE_PTR,
    P_SVP_HANDLE,
    P_CLIP_MODE,
    P_DPB_COMPRESS,
    P_DOLBY_VISION_LAYER,
    N_PROPS = P_DOLBY_VISION_LAYER
};
static const struct prop_def props[N_PROPS] = {
    {"decoded-size", "Decoded Data Length", G_TYPE_UINT64, 0, NULL, R},
    {"current-pts", "Get current PTS", G_TYPE_UINT64, 0, NULL, R},
    {"thumbnail-mode", "thumbnail generation mode", G_TYPE_BOOLEAN, 0},
    {"enlarge-max-lateness", "Enable check PTS for WiDi", G_TYPE_BOOLEAN, 0},
    {"interleaving-type", "support vudu 3D", G_TYPE_INT, 0},
    {"undecoded-size", "amount of undecoded data in CPB(bytes)", G_TYPE_UINT64, 0, NULL, R},
    {"svp-version",
     "Secure Video Path. 0 for normal play, 10 for WebOS 1.0 SVP, 20 for WebOS 1.5 SVP.",
     G_TYPE_UINT, 0},
    {"app-type", "Set application type", G_TYPE_STRING, 0},
    {"stream-width", "stream-width", G_TYPE_INT, 0, NULL, R},
    {"stream-height", "stream-height", G_TYPE_INT, 0, NULL, R},
    {"framerate-num", "framerate-num", G_TYPE_INT, 0, NULL, R},
    {"framerate-denom", "framerate-denom", G_TYPE_INT, 0, NULL, R},
    {"vdec-ch", "VDEC channel", G_TYPE_INT, 0},
    {"dish-trick", "dish-trick", G_TYPE_BOOLEAN, 0},
    {"dish-trick-ignore-rate", "dish-trick-ignore-rate", G_TYPE_BOOLEAN, 0},
    {"resource-info", "resource information about core-type, video-port, audio-port",
     PROP_STRUCTURE, 0, NULL, W},
    {"svp-mem-size", "Total Memory Size of CPB", G_TYPE_UINT, 0},
    {"svp-align-size", "Alienment Size", G_TYPE_UINT, 0},
    {"svp-baseaddr", "base address of secured CPB", G_TYPE_UINT, 0},
    {"svp-read-ptr", "current read pointer of secured CPB ", G_TYPE_UINT, 0},
    {"svp-write-ptr", "current write pointer of secured CPB ", G_TYPE_UINT, 0},
    {"svp-handle", "Get decoder handle for SVP", G_TYPE_UINT64, 0},
    {"clip-mode", "clip mode on/off", G_TYPE_BOOLEAN, 0},
    {"dpb-compress", "Do DPB Compression if possible", G_TYPE_BOOLEAN, 1},
    {"dolby-vision-layer", "Dolby-vision video layer", G_TYPE_STRING, 0, "none"},
};
#endif

typedef struct {
    GstElement parent;
    struct es_dec dec; /* first after the parent: esdec.c */
    GValue values[N_PROPS];
    struct stream_info info;
} GstLXVideoDec;
typedef struct {
    GstElementClass parent_class;
    void (*video_underrun)(GstLXVideoDec *self);
#ifdef SVP_HANDLE_POINTER
    void (*svp_handle)(GstLXVideoDec *self, gpointer arg);
#else
    void (*svp_handle)(GstLXVideoDec *self, guint arg);
#endif
    GstBuffer *(*convert_frame)(GstLXVideoDec *self, GstCaps *caps);
} GstLXVideoDecClass;

static GType gst_lx_video_dec_get_type(void);
G_DEFINE_TYPE(GstLXVideoDec, gst_lx_video_dec, GST_TYPE_ELEMENT)

/* Raw caps for the stream, with what is known of its size and rate (the sink takes the stream's
 * parameters from the stream's own caps, ES_CAPS_FIELD; these only have to be fixed). */
static GstCaps *raw_caps(GstElement *element, GstCaps *es)
{
    GstLXVideoDec *self = (GstLXVideoDec *)element;
    struct stream_info info;
    stream_info_from_caps(&info, es);
    GST_OBJECT_LOCK(self);
    self->info = info;
    GST_OBJECT_UNLOCK(self);
    return gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", "width", G_TYPE_INT,
                               info.width >= 16 ? (gint)info.width : 1920, "height", G_TYPE_INT,
                               info.height >= 16 ? (gint)info.height : 1080, "framerate",
                               GST_TYPE_FRACTION, info.fps_n > 0 ? info.fps_n : 0,
                               info.fps_n > 0 ? info.fps_d : 1, "pixel-aspect-ratio",
                               GST_TYPE_FRACTION, (gint)info.par_n, (gint)info.par_d, NULL);
}

static void gst_lx_video_dec_set_property(GObject *obj, guint id, const GValue *value,
                                          GParamSpec *spec)
{
    GstLXVideoDec *self = (GstLXVideoDec *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(value, &self->values[id - 1]);
    if (id == P_RESOURCE_INFO) {
        /* LG's decoder takes its VDEC channel from the video port the resource manager gave */
        gint port;
        const GstStructure *s = gst_value_get_structure(value);
        gchar *str = s ? gst_structure_to_string(s) : NULL;
        if (resource_info_int(s, "video-port", &port))
            g_value_set_int(&self->values[P_VDEC_CH - 1], port);
        GST_OBJECT_UNLOCK(self);
        syslog(LOG_INFO, "%s: resource-info %s", GST_OBJECT_NAME(self), str ? str : "(null)");
        g_free(str);
        return;
    }
    GST_OBJECT_UNLOCK(self);
    GST_DEBUG_OBJECT(self, "%s set", spec->name);
}

static void gst_lx_video_dec_get_property(GObject *obj, guint id, GValue *value, GParamSpec *spec)
{
    GstLXVideoDec *self = (GstLXVideoDec *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    switch (id) {
    case P_DECODED_SIZE:
    case P_UNDECODED_SIZE:
    case P_CURRENT_PTS:
        g_value_set_uint64(value, 0);
        break; /* nothing waits in a decoder */
    case P_STREAM_WIDTH:
        g_value_set_int(value, (gint)self->info.width);
        break;
    case P_STREAM_HEIGHT:
        g_value_set_int(value, (gint)self->info.height);
        break;
    case P_FRAMERATE_NUM:
        g_value_set_int(value, self->info.fps_n);
        break;
    case P_FRAMERATE_DENOM:
        g_value_set_int(value, self->info.fps_n > 0 ? self->info.fps_d : 0);
        break;
    default:
        g_value_copy(&self->values[id - 1], value);
        break;
    }
    GST_OBJECT_UNLOCK(self);
}

#ifdef SVP_HANDLE_POINTER
static void svp_handle(GstLXVideoDec *self, gpointer arg)
{
    GST_WARNING_OBJECT(self, "svp-handle: secure video path is unsupported");
}
#else
static void svp_handle(GstLXVideoDec *self, guint arg)
{
    GST_INFO_OBJECT(self, "svp-handle %u: no secure video path", arg);
}
#endif

static GstBuffer *convert_frame(GstLXVideoDec *self, GstCaps *caps)
{
    GST_INFO_OBJECT(self, "convert-frame: the decoded frames are on the host");
    return NULL;
}

static void gst_lx_video_dec_finalize(GObject *obj)
{
    es_dec_clear((struct es_dec_element *)obj);
    props_clear(((GstLXVideoDec *)obj)->values, N_PROPS);
    G_OBJECT_CLASS(gst_lx_video_dec_parent_class)->finalize(obj);
}

static void gst_lx_video_dec_class_init(GstLXVideoDecClass *klass)
{
    GObjectClass *gobject = G_OBJECT_CLASS(klass);
    GstElementClass *element = GST_ELEMENT_CLASS(klass);
    gobject->set_property = gst_lx_video_dec_set_property;
    gobject->get_property = gst_lx_video_dec_get_property;
    gobject->finalize = gst_lx_video_dec_finalize;
    props_install(gobject, props, N_PROPS);
    klass->svp_handle = svp_handle;
    klass->convert_frame = convert_frame;
    g_signal_new("video-underrun", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstLXVideoDecClass, video_underrun), NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
    g_signal_new("svp-handle", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 G_STRUCT_OFFSET(GstLXVideoDecClass, svp_handle), NULL, NULL,
#ifdef SVP_HANDLE_POINTER
                 g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
#else
                 g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
#endif
    g_signal_new("convert-frame", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 G_STRUCT_OFFSET(GstLXVideoDecClass, convert_frame), NULL, NULL,
                 g_cclosure_marshal_generic, GST_TYPE_BUFFER, 1, GST_TYPE_CAPS);
#ifdef O22N2_MEDIA
    /* No guest VDEC handle or decoded-frame readback exists. GObject's default
     * pointer/buffer action result is NULL, never a fabricated hardware handle. */
    g_signal_new("acquire-vdec-handle", G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_generic,
                 G_TYPE_POINTER, 2, G_TYPE_UINT, GST_TYPE_CAPS);
    g_signal_new("corrupted-frame", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
    g_signal_new("forced-preroll", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0,
                 NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
#endif
    gst_element_class_add_pad_template(element, gst_static_pad_template_get(&sink_template));
    gst_element_class_add_pad_template(element, gst_static_pad_template_get(&src_template));
    gst_element_class_set_static_metadata(
        element, "LX video decoder", "Codec/Decoder/Video",
        "LG's video decoder for the emulator: passes the stream on to the sink, which sends it "
        "to the host",
        "Beanjar");
}

static void gst_lx_video_dec_init(GstLXVideoDec *self)
{
    GstElementClass *klass = GST_ELEMENT_GET_CLASS(self);
    props_init(self->values, props, N_PROPS);
    es_dec_init((struct es_dec_element *)self, gst_element_class_get_pad_template(klass, "sink"),
                gst_element_class_get_pad_template(klass, "src"), raw_caps);
    es_dec_report_underrun((struct es_dec_element *)self);
}

/* The second factory, lxvideodec, as its own type: an element's factory is its type's. */
typedef GstLXVideoDec GstLXVideoDecPlain;
typedef GstLXVideoDecClass GstLXVideoDecPlainClass;
static GType gst_lx_video_dec_plain_get_type(void);
G_DEFINE_TYPE(GstLXVideoDecPlain, gst_lx_video_dec_plain, gst_lx_video_dec_get_type())
static void gst_lx_video_dec_plain_class_init(GstLXVideoDecPlainClass *klass)
{
}
static void gst_lx_video_dec_plain_init(GstLXVideoDecPlain *self)
{
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(lxvideodec_debug, "lxvideodec", 0, "LX video decoder (emulator)");
    /* the stock ranks; gstcool.conf raises omx_lxvideodec to 290 */
    return gst_element_register(plugin, "omx_lxvideodec", GST_RANK_SECONDARY,
                                gst_lx_video_dec_get_type()) &&
           gst_element_register(plugin, "lxvideodec", GST_RANK_PRIMARY,
                                gst_lx_video_dec_plain_get_type());
}

#define PACKAGE "beanjar"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, lxvideodec,
                  "LG's LX video decoder for the emulator", plugin_init, "1.0", "LGPL", PACKAGE,
                  "Beanjar")
