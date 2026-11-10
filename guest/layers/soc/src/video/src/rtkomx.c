// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libgstomx.so: Realtek's gst-omx plugin (W19P, W20L, W21P) for the emulator.
 *
 * The stock plugin wraps the OpenMAX components omx_vdec and omx_adec (libomx_core.so over
 * Realtek's video and audio CPUs through librpccore): one factory per section of
 * /etc/xdg/gstomx.conf, named after the section, of the class its type-name names, at its rank.
 * Its video decoders hand dvovideosink pictures in DVO memory (video/x-raw(memory:dvo), NV12, with
 * physical addresses in a GstDvoBufferMeta), its audio decoders PCM to rtkalsasink. playbin3's
 * decodebin3 reaches them through LG's decproxy.
 *
 * Here the factories are registered the same way, from the same file (omxconf.c), with the stock
 * classes' metadata, pad templates and properties (rtkomxclasses.h, rtkomxprops.h; the stock
 * classes have no signals), and nothing is decoded: the decoders pass the compressed stream through
 * under raw caps of their src template that name the stream's own (esdec.c):
 * video/x-raw(memory:dvo) NV12 of the stream's size for video, S16LE 48 kHz stereo for audio. The
 * buffers are the stream's, in ordinary memory, without a DVO meta: only our dvovideosink and
 * rtkalsasink (dvosink.c, rtkalsa.c) take them, and send them to the host. RTK_REL is the release:
 * 19, 20 or 21. The later releases (number1 and on, built as 21) add decoder factories under their
 * earlier names (dec_bin_register). */
#include <gst/gst.h>
#include <string.h>
#include <syslog.h>
#include "props.h"
#include "esdec.h"
#include "streaminfo.h"
#include "omxconf.h"

GST_DEBUG_CATEGORY_STATIC(omx_debug);
#define GST_CAT_DEFAULT omx_debug

#ifndef RTK_REL
#error "RTK_REL: 19, 20 or 21"
#endif

enum omx_kind { OMX_VIDEO_DEC, OMX_AUDIO_DEC };
struct omx_class {
    const char *type_name;
    enum omx_kind kind;
    const char *long_name, *description, *author;
    const char *sink_caps, *src_caps;
};
static const struct omx_class omx_classes[] = {
#include "rtkomxclasses.h"
};

#define R G_PARAM_READABLE
#define W G_PARAM_WRITABLE
/* The properties of GstOMXVideoDec and GstOMXAudioDec (after GstVideoDecoder's and
 * GstAudioDecoder's own), as gst-inspect-1.0 lists the stock ones: vprops[], aprops[] */
#include "rtkomxprops.h"
#define N_VPROPS G_N_ELEMENTS(vprops)
#define N_APROPS G_N_ELEMENTS(aprops)
#define N_DPROPS                                                                                   \
    (G_N_ELEMENTS(vprops) > G_N_ELEMENTS(aprops) ? G_N_ELEMENTS(vprops) : G_N_ELEMENTS(aprops))

typedef struct {
    GstElement parent;
    struct es_dec dec; /* first after the parent: esdec.c */
    GValue values[N_DPROPS];
} GstOMXDec;
typedef struct {
    GstElementClass parent_class;
    const struct omx_class *omx;
    const struct prop_def *props;
    guint n_props;
} GstOMXDecClass;
#define OMX_DEC_CLASS(obj) ((GstOMXDecClass *)G_OBJECT_GET_CLASS(obj))

static GstElementClass *dec_parent_class;

/* The video envelope: Realtek's decoder output, NV12 in DVO memory, of the stream's size and
 * rate (the sink takes the stream from ES_CAPS_FIELD). LG's decbridge (decproxy) wants a format,
 * width and height in raw video caps. */
static GstCaps *video_raw_caps(GstElement *element, GstCaps *es)
{
    struct stream_info info;
    GstCaps *caps;
    stream_info_from_caps(&info, es);
    if (info.width < 16 || info.height < 16)
        syslog(LOG_WARNING,
               "%s: no size in the stream's caps: 1920x1080 in the decoder's caps until the sink "
               "finds the real one",
               GST_OBJECT_NAME(element));
    caps = gst_caps_new_simple(
        "video/x-raw", "format", G_TYPE_STRING, "NV12", "width", G_TYPE_INT,
        info.width >= 16 ? (gint)info.width : 1920, "height", G_TYPE_INT,
        info.height >= 16 ? (gint)info.height : 1080, "framerate", GST_TYPE_FRACTION,
        info.fps_n > 0 ? info.fps_n : 0, info.fps_n > 0 ? info.fps_d : 1, "pixel-aspect-ratio",
        GST_TYPE_FRACTION, (gint)(info.par_n ? info.par_n : 1), (gint)(info.par_d ? info.par_d : 1),
        "interlace-mode", G_TYPE_STRING,
        info.scan_type == SCAN_INTERLACED ? "interleaved" : "progressive", NULL);
    gst_caps_set_features(caps, 0, gst_caps_features_new("memory:dvo", NULL));
    return caps;
}

/* The audio envelope: what rtkalsasink takes as PCM; the stream's own rate and channels are in
 * ES_CAPS_FIELD. */
static GstCaps *audio_raw_caps(GstElement *element, GstCaps *es)
{
    return gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, "S16LE", "layout",
                               G_TYPE_STRING, "interleaved", "rate", G_TYPE_INT, 48000, "channels",
                               G_TYPE_INT, 2, NULL);
}

static void omx_dec_set_property(GObject *obj, guint id, const GValue *value, GParamSpec *spec)
{
    GstOMXDec *self = (GstOMXDec *)obj;
    GstOMXDecClass *klass = OMX_DEC_CLASS(obj);
    if (id < 1 || id > klass->n_props) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(value, &self->values[id - 1]);
    GST_OBJECT_UNLOCK(self);
    if (G_VALUE_HOLDS(value, GST_TYPE_STRUCTURE)) {
        const GstStructure *s = gst_value_get_structure(value);
        gchar *str = s ? gst_structure_to_string(s) : NULL;
        syslog(LOG_INFO, "%s: %s %s", GST_OBJECT_NAME(self), spec->name, str ? str : "(null)");
        g_free(str);
    }
}

static void omx_dec_get_property(GObject *obj, guint id, GValue *value, GParamSpec *spec)
{
    GstOMXDec *self = (GstOMXDec *)obj;
    GstOMXDecClass *klass = OMX_DEC_CLASS(obj);
    if (id < 1 || id > klass->n_props) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    if (!strcmp(spec->name, "decoded-size") || !strcmp(spec->name, "undecoded-size")) {
        g_value_set_uint64(value, 0); /* nothing waits in a decoder */
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(&self->values[id - 1], value);
    GST_OBJECT_UNLOCK(self);
}

static void omx_dec_finalize(GObject *obj)
{
    es_dec_clear((struct es_dec_element *)obj);
    props_clear(((GstOMXDec *)obj)->values, OMX_DEC_CLASS(obj)->n_props);
    G_OBJECT_CLASS(dec_parent_class)->finalize(obj);
}

static void omx_dec_base_class_init(gpointer g_class, gpointer data)
{
    GObjectClass *gobject = G_OBJECT_CLASS(g_class);
    GstOMXDecClass *klass = g_class;
    dec_parent_class = g_type_class_peek_parent(g_class);
    gobject->set_property = omx_dec_set_property;
    gobject->get_property = omx_dec_get_property;
    gobject->finalize = omx_dec_finalize;
    if (GPOINTER_TO_INT(data) == OMX_VIDEO_DEC) {
        klass->props = vprops;
        klass->n_props = N_VPROPS;
    } else {
        klass->props = aprops;
        klass->n_props = N_APROPS;
    }
    props_install(gobject, klass->props, klass->n_props);
}

static void omx_dec_class_init(gpointer g_class, gpointer data)
{
    GstElementClass *element = GST_ELEMENT_CLASS(g_class);
    GstOMXDecClass *klass = g_class;
    const struct omx_class *c = data;
    klass->omx = c;
    gst_element_class_add_pad_template(element,
                                       gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                            gst_caps_from_string(c->sink_caps)));
    gst_element_class_add_pad_template(element,
                                       gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                                            gst_caps_from_string(c->src_caps)));
    gst_element_class_set_metadata(element, c->long_name,
                                   c->kind == OMX_VIDEO_DEC ? "Codec/Decoder/Video"
                                                            : "Codec/Decoder/Audio",
                                   c->description, c->author);
}

static void omx_dec_init(GTypeInstance *instance, gpointer g_class)
{
    GstOMXDec *self = (GstOMXDec *)instance;
    GstOMXDecClass *klass = g_class;
    GstElementClass *element = g_class;
    props_init(self->values, klass->props, klass->n_props);
    es_dec_init((struct es_dec_element *)self, gst_element_class_get_pad_template(element, "sink"),
                gst_element_class_get_pad_template(element, "src"),
                klass->omx->kind == OMX_VIDEO_DEC ? video_raw_caps : audio_raw_caps);
    /* starvation: video-underrun from the sink (essink.c) */
    if (klass->omx->kind == OMX_VIDEO_DEC)
        es_dec_report_underrun((struct es_dec_element *)self);
    /* Something between the decoder and the sink (in decproxy's or playsink's part of the graph,
     * measured on W19P) cuts every buffer under these S16LE stereo caps down to whole frames of
     * 4 bytes: the AAC frames lost their last bytes. */
    else
        es_dec_length_prefix((struct es_dec_element *)self, 4);
}

static GType omx_dec_base_type(enum omx_kind kind)
{
    static gsize types[2];
    if (g_once_init_enter(&types[kind])) {
        GTypeInfo info = {sizeof(GstOMXDecClass),
                          NULL,
                          NULL,
                          omx_dec_base_class_init,
                          NULL,
                          GINT_TO_POINTER(kind),
                          sizeof(GstOMXDec),
                          0,
                          NULL,
                          NULL};
        GType t = g_type_register_static(
            GST_TYPE_ELEMENT, kind == OMX_VIDEO_DEC ? "GstOMXVideoDec" : "GstOMXAudioDec", &info,
            G_TYPE_FLAG_ABSTRACT);
        g_once_init_leave(&types[kind], t);
    }
    return types[kind];
}

static GType factory_type(const gchar *factory, const gchar *type_name)
{
    for (guint i = 0; i < G_N_ELEMENTS(omx_classes); i++) {
        const struct omx_class *c = &omx_classes[i];
        GType t;
        if (strcmp(c->type_name, type_name))
            continue;
        t = g_type_from_name(c->type_name);
        if (!t) {
            GTypeInfo info = {sizeof(GstOMXDecClass),
                              NULL,
                              NULL,
                              omx_dec_class_init,
                              NULL,
                              c,
                              sizeof(GstOMXDec),
                              0,
                              omx_dec_init,
                              NULL};
            t = g_type_register_static(omx_dec_base_type(c->kind), c->type_name, &info, 0);
        }
        return t;
    }
    return 0;
}

/* The decoder bins of Realtek's later gst-omx (number1 and later releases:
 * gst_omx_dec_bin_register, which calls gst_rtk_codec_bin_register in libgstrtkcodec-1.0). Their
 * gstomx.conf names each section "omx<codec>dec1", at rank 0, and for each section the pattern
 * "(omx(.+)dec)1" matches whose component-name is omx_vdec or omx_adec the plugin registers a bin
 * "omx<codec>dec" of type "GstOMX<Codec>DecBin" around the section's decoder, at primary + 1 (257),
 * or at none (0) if the name holds "auto". LG's decproxy picks only decoders at marginal or above,
 * so these are what it finds. Here the factory is a subtype of the section's decoder under the
 * stock name, rank and type name. The sections of the earlier releases ("omxh264dec") do not match:
 * nothing is added. */
static void dec_bin_register(GstPlugin *plugin, GKeyFile *conf, const gchar *section, GType type)
{
    GRegex *re = g_regex_new("(omx(.+)dec)1", 0, 0, NULL);
    GMatchInfo *m = NULL;
    if (g_regex_match(re, section, 0, &m)) {
        gchar *component = g_key_file_get_string(conf, section, "component-name", NULL);
        gchar *name = g_match_info_fetch(m, 1), *codec = g_match_info_fetch(m, 2);
        gchar *type_name =
            g_strdup_printf("GstOMX%c%sDecBin", g_ascii_toupper(codec[0]), codec + 1);
        if (!g_strcmp0(component, "omx_vdec") || !g_strcmp0(component, "omx_adec")) {
            GType t = g_type_from_name(type_name);
            if (!t) {
                GTypeQuery q;
                g_type_query(type, &q);
                t = g_type_register_static_simple(type, type_name, q.class_size, NULL,
                                                  q.instance_size, NULL, 0);
            }
            gst_element_register(plugin, name,
                                 strstr(name, "auto") ? GST_RANK_NONE : GST_RANK_PRIMARY + 1, t);
        }
        g_free(type_name);
        g_free(codec);
        g_free(name);
        g_free(component);
    }
    g_match_info_free(m);
    g_regex_unref(re);
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(omx_debug, "omx", 0, "Realtek's gst-omx (emulator)");
    omx_conf_register(plugin, factory_type, dec_bin_register);
    return TRUE;
}

#define PACKAGE "beanjar"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, omx,
                  "Realtek's GStreamer OpenMAX plug-ins for the emulator", plugin_init, "1.0.0",
                  "LGPL", PACKAGE, "Beanjar")
