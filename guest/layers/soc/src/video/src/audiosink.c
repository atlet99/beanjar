// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* "audio_sink": W17H's GstAdecSink (libgstaudiosink-1.0.so) for the emulator.
 *
 * The stock sink plays audio_dec's PCM on LG1313's audio output and provides the pipeline clock
 * from the hardware (gst_lxclock, /dev/lg/clk0); it is gstcool.conf's default audio sink. Here
 * it gets the compressed stream that our audio_dec passes through under raw caps naming the
 * stream's own (esdec.c), or really raw PCM straight from the demuxer, and sends either to the
 * host as h13's adecsink does (essink.c), providing a monotonic GstSystemClock as the pipeline
 * clock, which the host follows. */
#include <gst/gst.h>
#include <string.h>
#include "props.h"
#include "essink.h"

GST_DEBUG_CATEGORY_STATIC(audiosink_debug);
#define GST_CAT_DEFAULT audiosink_debug

/* The stock template */
#if defined(O22N2_MEDIA) && LX_REL >= 24
#define SINK_CHANNELS "10"
#else
#define SINK_CHANNELS "8"
#endif
#define SINK_CAPS                                                                                  \
    "audio/x-raw, format = (string) { S8, U8, S16LE, S16BE, U16LE, U16BE, S24LE, "                 \
    "S24BE, U24LE, U24BE, S32LE, S32BE, U32LE, U32BE }, layout = (string) interleaved, "           \
    "rate = (int) [ 1, 384000 ], channels = (int) [ 1, " SINK_CHANNELS " ]; "

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(SINK_CAPS));

#define R G_PARAM_READABLE
#define W G_PARAM_WRITABLE
/* As gst-inspect-1.0 lists GstAdecSink's own (W17H, under qemu-user), after GstBaseSink's. */
#ifdef O22N2_MEDIA
#include "o22n2-audiosink-props.h"
#else
enum {
    P_PLAY_SPEED = 1,
    P_DECODED_SIZE,
    P_UNDECODED_SIZE,
    P_CURRENT_PTS,
    P_SERVERSIDE_TRICKPLAY,
    P_LIPSYNC_OFFSET,
    P_DISABLE_GSTSYNC,
    P_LIPSYNC,
    P_NODELAY,
    P_NODELAY_MODE,
    P_NODELAY_SKIP,
    P_NODELAY_FAST,
    P_NODELAY_RECOVER,
    P_NODELAY_FAST_FREQ,
    P_APP_TYPE,
    P_RESOURCE_INFO,
    P_MIXER,
    P_INDEX,
    P_AUDIO_ONLY,
    P_SEAMLESS,
    P_MUTE,
    P_FADE,
    P_FADE_VOLUME,
    P_CLOCK_SOURCE,
    P_VERSION,
    N_PROPS = P_VERSION
};
static const struct prop_def props[N_PROPS] = {
    {"play-speed", "Play Speed", G_TYPE_INT, 1000},
    {"decoded-size", "Decoded Data Length", G_TYPE_UINT64, 0},
    {"undecoded-size", "UnDecoded Data Length", G_TYPE_UINT64, 0, NULL, R},
    {"current-pts", "Get current PTS", G_TYPE_UINT64, 0},
    {"serverside-trickplay", "Set server's trick play mode", G_TYPE_BOOLEAN, 0},
    {"lipsync-offset", "Audio's lipsync offset(msec)", G_TYPE_UINT, 268435455},
    {"disable-gstsync", "Disable Basesink's Sync", G_TYPE_BOOLEAN, 0},
    {"lipsync", "Set Lipsync Mode", G_TYPE_BOOLEAN, 1},
    {"nodelay", "Set No Delay Mode", G_TYPE_BOOLEAN, 0},
    {"nodelay-mode", "Nodelay Mode (0: CONT, 1: SKIP, 2: HYBRID)", G_TYPE_UINT, 0xffffffff},
    {"nodelay-skip", "Buffering Threshold of Skipping", G_TYPE_UINT, 0xffffffff},
    {"nodelay-fast", "Buffering Threshold of Fast", G_TYPE_UINT, 0xffffffff},
    {"nodelay-recover", "Buffering Threshold of Normal State", G_TYPE_UINT, 0xffffffff},
    {"nodelay-fast-freq", "Frequency of Fast State", G_TYPE_UINT, 0xffffffff},
    {"app-type", "Set application type", G_TYPE_STRING, 0},
    {"resource-info", "Set the port of audio resource", PROP_STRUCTURE, 0, NULL, W},
    {"mixer", "Set using mixer", G_TYPE_BOOLEAN, 0},
    {"index", "Index of audio resource", G_TYPE_UINT, 0xffffffff},
    {"audio-only", "This stream has audio only", G_TYPE_BOOLEAN, 0},
    {"seamless", "Seamless audio change", G_TYPE_BOOLEAN, 0},
    {"mute", "Control sound mute", G_TYPE_BOOLEAN, 0},
    {"fade", "Set the fade volume", PROP_STRUCTURE, 0, NULL, W},
    {"fade-volume", "get the fade volume", G_TYPE_INT, -1, NULL, R},
    {"clock-source", "acquire the source of clock", G_TYPE_STRING, 0},
    {"version", "Version of audio sink", G_TYPE_UINT, 20180329, NULL, R},
};
#endif

typedef struct {
    EsSink parent;
    GValue values[N_PROPS];
} GstAdecSink;
typedef struct {
    EsSinkClass parent_class;
    void (*audio_underrun)(GstAdecSink *self);
} GstAdecSinkClass;

static GType gst_adec_sink_get_type(void);
G_DEFINE_TYPE(GstAdecSink, gst_adec_sink, es_sink_get_type())

static void gst_adec_sink_set_property(GObject *obj, guint id, const GValue *value,
                                       GParamSpec *spec)
{
    GstAdecSink *self = (GstAdecSink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(value, &self->values[id - 1]);
    GST_OBJECT_UNLOCK(self);
    GST_DEBUG_OBJECT(self, "%s set", spec->name);
#ifdef O22N2_MEDIA
    if (id == P_RESOURCE_INFO)
        es_sink_resource_info(ES_SINK(self), value, "audio-port");
#endif
    if (id == P_INDEX && g_value_get_uint(value) <= 7) /* the audio resource's index: the channel */
        es_sink_set_channel(ES_SINK(self), (gint)g_value_get_uint(value));
}

static void gst_adec_sink_get_property(GObject *obj, guint id, GValue *value, GParamSpec *spec)
{
    GstAdecSink *self = (GstAdecSink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    if (id == P_CURRENT_PTS) {
        /* what the host plays now: the clock-based position */
        g_value_set_uint64(value, es_sink_position(ES_SINK(self)));
        return;
    }
    GST_OBJECT_LOCK(self);
    if (id == P_DECODED_SIZE || id == P_UNDECODED_SIZE)
        g_value_set_uint64(value, 0);
    else
        g_value_copy(&self->values[id - 1], value);
    GST_OBJECT_UNLOCK(self);
}

static void gst_adec_sink_finalize(GObject *obj)
{
    props_clear(((GstAdecSink *)obj)->values, N_PROPS);
    G_OBJECT_CLASS(gst_adec_sink_parent_class)->finalize(obj);
}

static void gst_adec_sink_class_init(GstAdecSinkClass *klass)
{
    GObjectClass *gobject = G_OBJECT_CLASS(klass);
    GstElementClass *element = GST_ELEMENT_CLASS(klass);
    gobject->set_property = gst_adec_sink_set_property;
    gobject->get_property = gst_adec_sink_get_property;
    gobject->finalize = gst_adec_sink_finalize;
    props_install(gobject, props, N_PROPS);
    klass->parent_class.kind = WEBOS_MEDIA_AUDIO;
    g_signal_new("audio-underrun", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstAdecSinkClass, audio_underrun), NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
#ifdef O22N2_MEDIA
#if LX_REL >= 24
    g_signal_new("es-bypass-done", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
#endif
    g_signal_new("clock-calibrated", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
#if LX_REL >= 26
    /* W22O queue's action; nothing is queued here to drop */
    g_signal_new("audio-sample-drop", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 0, NULL, NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
#endif
#endif
    gst_element_class_add_pad_template(element, gst_static_pad_template_get(&sink_template));
    gst_element_class_set_static_metadata(
        element, "audio sink", "Sink/Audio",
        "LG's audio sink for the emulator: provides the clock, sends the audio to the host viewer",
        "Beanjar");
}

static void gst_adec_sink_init(GstAdecSink *self)
{
    props_init(self->values, props, N_PROPS);
    /* The pipeline clock, as LG's sink provides one: a monotonic system clock of our own. */
    es_sink_provide_clock(ES_SINK(self), "audio_sink-clock");
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(audiosink_debug, "audio_sink", 0, "audio sink (emulator)");
#ifdef O22N2_MEDIA
    /* secondary in each firmware's own registration, W24G's too */
    return gst_element_register(plugin, "audiosink", GST_RANK_SECONDARY, gst_adec_sink_get_type());
#else
    return gst_element_register(plugin, "audio_sink", GST_RANK_PRIMARY, gst_adec_sink_get_type());
#endif
}

#define PACKAGE "beanjar"
#ifdef O22N2_MEDIA
#define AUDIO_PLUGIN lgaudiosink
#else
#define AUDIO_PLUGIN audio_sink
#endif
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, AUDIO_PLUGIN,
                  "LG's audio sink for the emulator", plugin_init, "1.0", "LGPL", PACKAGE,
                  "Beanjar")
