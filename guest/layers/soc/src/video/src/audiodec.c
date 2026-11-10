// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* "audio_dec": W17H's GstADec (libgstaudiodec-1.0.so, plugin lg_audio_dec) for the emulator.
 *
 * The stock element decodes on LG1313's audio DSP into PCM for audio_sink; playbin3's
 * decodebin3 reaches it through LG's decproxy (gstcool.conf ranks it 290), libpf links it
 * decoder -> queue -> sink for MSE. This one keeps its surface (factory name and rank, pad
 * templates, properties) and decodes nothing: the compressed buffers go on to the sink
 * unchanged under raw caps that name the stream's own (esdec.c), and audio_sink (audiosink.c)
 * sends them to the host. The stock plugin's second factory, ac4_dec (AC-4), is left out: no
 * stream here has AC-4, and the host has no AC-4 decoder.
 *
 * The modern LX plugin (lgaudiodec) has one factory per codec, all with the same properties and
 * rank (W22O 43.21's gst-inspect). Those of the codecs the host decodes are here: AAC, AC-3 and
 * E-AC-3, Opus, Vorbis and FLAC; YouTube's VOD audio is Opus, its live audio AAC. The rest
 * (ADPCM, PCM, AC-4, AMR, MPEG-H, IAMF, WMA) are left out. */
#include <gst/gst.h>
#include <string.h>
#include <syslog.h>
#include "props.h"
#include "esdec.h"

GST_DEBUG_CATEGORY_STATIC(audiodec_debug);
#define GST_CAT_DEFAULT audiodec_debug

/* The stock templates, verbatim */
#ifdef O22N2_MEDIA
/* The stock factories: name, GType name (which gives the element's default name), long-name
 * and sink template */
static const struct adec_factory {
    const char *factory, *type, *longname, *description, *caps;
} factories[] = {
    {"aac_audiodec", "GstLgAudioDecAac", "AAC audio decoder", "LG's AAC audio decoder element",
     "audio/mpeg, mpegversion = (int) { 2, 4 }"},
    {"ac3_audiodec", "GstLgAudioDecAc3", "AC3 audio decoder", "LG's AC3 audio decoder element",
     "audio/x-ac3; audio/x-eac3; audio/x-private1-ac3"},
    {"vorbis_audiodec", "GstLgAudioDecVorbis", "VORBIS audio decoder",
     "LG's VORBIS audio decoder element", "audio/x-vorbis"},
    {"opus_audiodec", "GstLgAudioDecOpus", "OPUS audio decoder", "LG's OPUS audio decoder element",
     "audio/x-opus"},
    {"flac_audiodec", "GstLgFlacAudioDecFlac", "FLAC audio decoder",
     "LG's FLAC audio decoder element", "audio/x-flac"},
};
#else
#define SINK_CAPS                                                                                  \
    "audio/mpeg; audio/mpeg-h; audio/x-dts; audio/x-private1-dts; audio/x-dtsh; "                  \
    "audio/x-dtse; audio/x-dtsl; audio/x-ac3; audio/x-eac3; audio/x-private1-ac3; audio/x-wma; "   \
    "audio/x-pn-realaudio; audio/x-lpcm-1; audio/x-lpcm; audio/x-private-lg-lpcm; "                \
    "audio/x-private1-lpcm; audio/x-private-ts-lpcm; audio/x-adpcm; audio/x-vorbis; audio/AMR; "   \
    "audio/AMR-WB; audio/x-flac; audio/x-mulaw; audio/x-alaw; audio/x-fd; "
#endif
#define SRC_CAPS                                                                                   \
    "audio/x-raw, format = (string) { S8, U8, S16LE, S16BE, U16LE, U16BE, S24LE, "                 \
    "S24BE, U24LE, U24BE, S32LE, S32BE, U32LE, U32BE }, layout = (string) interleaved, "           \
    "rate = (int) [ 1, 384000 ], channels = (int) [ 1, 8 ]"

#ifndef O22N2_MEDIA
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(SINK_CAPS));
#endif
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SRC_CAPS));

#define R G_PARAM_READABLE
#define W G_PARAM_WRITABLE
/* As gst-inspect-1.0 lists GstADec's (W17H, under qemu-user). */
#ifdef O22N2_MEDIA
#include "o22n2-aac_audiodec-props.h"
#else
enum {
    P_DECODED_SIZE = 1,
    P_UNDECODED_SIZE,
    P_SERVERSIDE_TRICKPLAY,
    P_APP_TYPE,
    P_RESOURCE_INFO,
    P_INDEX,
    P_SEAMLESS,
    P_MPEG_DASH,
    P_DTS_SEAMLESS,
    P_PLAY_SPEED,
    P_DUAL_MONO,
    P_VERSION,
    N_PROPS = P_VERSION
};
static const struct prop_def props[N_PROPS] = {
    {"decoded-size", "Decoded Data Length", G_TYPE_UINT64, 0, NULL, R},
    {"undecoded-size", "UnDecoded Data Length", G_TYPE_UINT64, 0, NULL, R},
    {"serverside-trickplay", "Set server's trick play mode", G_TYPE_BOOLEAN, 0},
    {"app-type", "Set application type", G_TYPE_STRING, 0},
    {"resource-info", "Set the port of audio resource", PROP_STRUCTURE, 0, NULL, W},
    {"index", "Index of audio resource", G_TYPE_UINT, 0xffffffff},
    {"seamless", "Seamless audio change", G_TYPE_BOOLEAN, 0},
    {"mpeg-dash", "MPEG-DASH player", G_TYPE_BOOLEAN, 0},
    {"dts-seamless", "Digital Delivery Parser of DTS-M6", G_TYPE_BOOLEAN, 0},
    {"play-speed", "Play Speed", G_TYPE_INT, 1000},
    {"dual-mono", "Audio output mode of decoder element (Dual mono)", G_TYPE_UINT, 1},
    {"version", "Version of audio decoder element", G_TYPE_STRING, 0, "v1.00.00-2018.03.29", R},
};
#endif

typedef struct {
    GstElement parent;
    struct es_dec dec; /* first after the parent: esdec.c */
    GValue values[N_PROPS];
} GstADec;
typedef struct {
    GstElementClass parent_class;
} GstADecClass;

static GType gst_adec_get_type(void);
#ifdef O22N2_MEDIA
G_DEFINE_ABSTRACT_TYPE(GstADec, gst_adec, GST_TYPE_ELEMENT)
#else
G_DEFINE_TYPE(GstADec, gst_adec, GST_TYPE_ELEMENT)
#endif

/* Raw caps for the stream (the sink sends the stream's own caps, ES_CAPS_FIELD; these only have
 * to be fixed): its rate and channels where the caps have them. */
static GstCaps *raw_caps(GstElement *element, GstCaps *es)
{
    gint rate = 0, channels = 0;
    if (gst_caps_get_size(es)) {
        const GstStructure *s = gst_caps_get_structure(es, 0);
        gst_structure_get_int(s, "rate", &rate);
        gst_structure_get_int(s, "channels", &channels);
    }
    return gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, "S16LE", "layout",
                               G_TYPE_STRING, "interleaved", "rate", G_TYPE_INT,
                               rate > 0 && rate <= 384000 ? rate : 48000, "channels", G_TYPE_INT,
                               channels > 0 && channels <= 8 ? channels : 2, NULL);
}

static void gst_adec_set_property(GObject *obj, guint id, const GValue *value, GParamSpec *spec)
{
    GstADec *self = (GstADec *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(value, &self->values[id - 1]);
    GST_OBJECT_UNLOCK(self);
    GST_DEBUG_OBJECT(self, "%s set", spec->name);
}

static void gst_adec_get_property(GObject *obj, guint id, GValue *value, GParamSpec *spec)
{
    GstADec *self = (GstADec *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    if (id == P_DECODED_SIZE || id == P_UNDECODED_SIZE)
        g_value_set_uint64(value, 0); /* nothing waits in a decoder */
    else
        g_value_copy(&self->values[id - 1], value);
    GST_OBJECT_UNLOCK(self);
}

static void gst_adec_finalize(GObject *obj)
{
    es_dec_clear((struct es_dec_element *)obj);
    props_clear(((GstADec *)obj)->values, N_PROPS);
    G_OBJECT_CLASS(gst_adec_parent_class)->finalize(obj);
}

static void gst_adec_class_init(GstADecClass *klass)
{
    GObjectClass *gobject = G_OBJECT_CLASS(klass);
    GstElementClass *element = GST_ELEMENT_CLASS(klass);
    gobject->set_property = gst_adec_set_property;
    gobject->get_property = gst_adec_get_property;
    gobject->finalize = gst_adec_finalize;
    props_install(gobject, props, N_PROPS);
#ifdef O22N2_MEDIA
    g_signal_new("dialog-enhancement-limit-change", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                 NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
#endif
    gst_element_class_add_pad_template(element, gst_static_pad_template_get(&src_template));
#ifndef O22N2_MEDIA
    gst_element_class_add_pad_template(element, gst_static_pad_template_get(&sink_template));
    gst_element_class_set_static_metadata(
        element, "audio decoder", "Codec/Decoder/Audio",
        "LG's audio decoder for the emulator: passes the stream on to the sink, which sends it "
        "to the host",
        "Beanjar");
#endif
}

#ifdef O22N2_MEDIA
/* One subclass per stock factory, for its own sink template and metadata */
static void gst_adec_codec_class_init(gpointer klass, gpointer data)
{
    const struct adec_factory *f = data;
    GstElementClass *element = GST_ELEMENT_CLASS(klass);
    gst_element_class_add_pad_template(
        element,
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string(f->caps)));
    gst_element_class_set_static_metadata(element, f->longname, "Codec/Decoder/Audio",
                                          f->description, "Beanjar");
}
#endif

static void gst_adec_init(GstADec *self)
{
    props_init(self->values, props, N_PROPS);
#ifndef O22N2_MEDIA
    GstElementClass *klass = GST_ELEMENT_GET_CLASS(self);
    es_dec_init((struct es_dec_element *)self, gst_element_class_get_pad_template(klass, "sink"),
                gst_element_class_get_pad_template(klass, "src"), raw_caps);
#endif
}

#ifdef O22N2_MEDIA
/* The pads from the factory's own class (during the base's init the instance has the base's) */
static void gst_adec_codec_init(GTypeInstance *instance, gpointer klass)
{
    es_dec_init((struct es_dec_element *)instance,
                gst_element_class_get_pad_template(klass, "sink"),
                gst_element_class_get_pad_template(klass, "src"), raw_caps);
}
#endif

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(audiodec_debug, "audio_dec", 0, "audio decoder (emulator)");
    /* the stock rank; gstcool.conf raises it to 290 */
#ifdef O22N2_MEDIA
    for (unsigned i = 0; i < G_N_ELEMENTS(factories); i++) {
        GTypeQuery q;
        g_type_query(gst_adec_get_type(), &q);
        const GTypeInfo info = {
            .class_size = q.class_size,
            .class_init = gst_adec_codec_class_init,
            .class_data = &factories[i],
            .instance_size = q.instance_size,
            .instance_init = gst_adec_codec_init,
        };
        GType type = g_type_register_static(gst_adec_get_type(), factories[i].type, &info, 0);
        if (!gst_element_register(plugin, factories[i].factory, GST_RANK_SECONDARY, type))
            return FALSE;
    }
    return TRUE;
#else
    return gst_element_register(plugin, "audio_dec", GST_RANK_PRIMARY, gst_adec_get_type());
#endif
}

#define PACKAGE "beanjar"
#ifdef O22N2_MEDIA
#define AUDIO_PLUGIN lgaudiodec
#else
#define AUDIO_PLUGIN lg_audio_dec
#endif
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, AUDIO_PLUGIN,
                  "LG's audio decoder for the emulator", plugin_init, "1.0", "LGPL", PACKAGE,
                  "Beanjar")
