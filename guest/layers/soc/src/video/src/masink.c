// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* "masink": MStar's GstMasink (libgstmasink.so, W16R and W18R) for the emulator.
 *
 * The stock sink, a GstAudioBaseSink, plays S16LE PCM through MSIL's audio DMA reader on the
 * audio DSP and provides the pipeline clock; gstcool.conf's default audio sink is omxaudiosink,
 * and masink (rank 128) is the other one a pipeline may pick. Here it sends what it gets to the
 * host as omxaudiosink does (essink.c), with the stock properties: GstAudioBaseSink's, which it
 * only keeps, then MStar's. */
#include <gst/gst.h>
#include <string.h>
#include "props.h"
#include "essink.h"

GST_DEBUG_CATEGORY_STATIC(masink_debug);
#define GST_CAT_DEFAULT masink_debug

/* libgstaudio's, GstAudioBaseSink's enum (gst-plugins-base has no headers here) */
GType gst_audio_base_sink_slave_method_get_type(void);

/* The stock template */
#define SINK_CAPS "audio/x-raw, format = S16LE , channels = (int) [1, 2], rate = (int) [8000,48000]"

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(SINK_CAPS));

/* As gst-inspect-1.0 lists GstMasink's, after GstBaseSink's (W16R and W18R alike). */
enum {
    P_BUFFER_TIME = 1,
    P_LATENCY_TIME,
    P_PROVIDE_CLOCK,
    P_SLAVE_METHOD,
    P_CAN_ACTIVATE_PULL,
    P_ALIGNMENT_THRESHOLD,
    P_DRIFT_TOLERANCE,
    P_DISCONT_WAIT,
    P_VOLUME,
    P_SPDIF,
    P_HDMI,
    P_MUTE,
    P_APP_TYPE,
    P_DISABLE_LOST_STATE,
    N_PROPS = P_DISABLE_LOST_STATE
};
static const struct prop_def props[N_PROPS] = {
    {"buffer-time",
     "Size of audio buffer in microseconds, this is the minimum latency that the sink reports",
     G_TYPE_INT64, 200000},
    {"latency-time", "The minimum amount of data to write in each iteration in microseconds",
     G_TYPE_INT64, 10000},
    {"provide-clock", "Provide a clock to be used as the global pipeline clock", G_TYPE_BOOLEAN, 0},
    {"slave-method", "Algorithm to use to match the rate of the masterclock", G_TYPE_ENUM, 1, NULL,
     0, gst_audio_base_sink_slave_method_get_type},
    {"can-activate-pull", "Allow pull-based scheduling", G_TYPE_BOOLEAN, 0},
    {"alignment-threshold", "Timestamp alignment threshold in nanoseconds", G_TYPE_UINT64,
     400000000},
    {"drift-tolerance", "Tolerance for clock drift in microseconds", G_TYPE_INT64, 40000},
    {"discont-wait", "Window of time in nanoseconds to wait before creating a discontinuity",
     G_TYPE_UINT64, 1000000000},
    {"volume", "Volume of this stream", G_TYPE_UINT, 100},
    {"SPDIF", "SPDIF NON-PCM output mode", G_TYPE_BOOLEAN, 0},
    {"HDMI", "HDMI NON-PCM output mode", G_TYPE_BOOLEAN, 0},
    {"mute", "mute audio output", G_TYPE_BOOLEAN, 0},
    {"app-type", "Set app type", G_TYPE_STRING, 0},
    {"disable-lost-state", "Disable seek event if TRUE", G_TYPE_BOOLEAN, 0},
};

typedef struct {
    EsSink parent;
    GValue values[N_PROPS];
} GstMasink;
typedef EsSinkClass GstMasinkClass;

static GType gst_masink_get_type(void);
G_DEFINE_TYPE(GstMasink, gst_masink, es_sink_get_type())

static void gst_masink_set_property(GObject *obj, guint id, const GValue *value, GParamSpec *spec)
{
    GstMasink *self = (GstMasink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(value, &self->values[id - 1]);
    GST_OBJECT_UNLOCK(self);
}

static void gst_masink_get_property(GObject *obj, guint id, GValue *value, GParamSpec *spec)
{
    GstMasink *self = (GstMasink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(&self->values[id - 1], value);
    GST_OBJECT_UNLOCK(self);
}

static void gst_masink_finalize(GObject *obj)
{
    props_clear(((GstMasink *)obj)->values, N_PROPS);
    G_OBJECT_CLASS(gst_masink_parent_class)->finalize(obj);
}

static void gst_masink_class_init(GstMasinkClass *klass)
{
    GObjectClass *gobject = G_OBJECT_CLASS(klass);
    GstElementClass *element = GST_ELEMENT_CLASS(klass);
    gobject->set_property = gst_masink_set_property;
    gobject->get_property = gst_masink_get_property;
    gobject->finalize = gst_masink_finalize;
    props_install(gobject, props, N_PROPS);
    klass->kind = WEBOS_MEDIA_AUDIO;
    gst_element_class_add_pad_template(element, gst_static_pad_template_get(&sink_template));
    gst_element_class_set_static_metadata(element, "MStar Audio Sink", "Sink/Audio",
                                          "MStar Audio Output",
                                          "kochien.kuo <kochien.kuo@mstarsemi.com>");
}

static void gst_masink_init(GstMasink *self)
{
    props_init(self->values, props, N_PROPS);
    es_sink_provide_clock(ES_SINK(self), "masink-clock");
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(masink_debug, "masink", 0, "MStar audio sink (emulator)");
    return gst_element_register(plugin, "masink", GST_RANK_SECONDARY, gst_masink_get_type());
}

#define PACKAGE "beanjar"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, masink, "MStar Audio Sink for the emulator",
                  plugin_init, "1.0", "LGPL", PACKAGE, "Beanjar")
