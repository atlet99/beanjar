// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* "rtkalsasink": Realtek's GstRtkAlsaSink (libgstrtkalsa.so, W19P, W20L, W21P) for the emulator.
 *
 * The stock sink, a GstRtkAudioBaseSink, plays the OMX audio decoders' PCM (or framed AC-3, E-AC-3,
 * DTS, MPEG-1 passthrough) on ALSA's hw:0,4 and the audio DSP (/dev/rtkaudio), and provides the
 * pipeline clock from the DSP's reference clock (GstRtkRefClock, reached through the
 * gst.rtk.audio.info context). Here the decoders (rtkomx.c) pass the compressed stream through
 * under S16LE caps that name the stream's own (esdec.c), and this sink sends it to the host
 * (essink.c), as it does PCM and passthrough streams that come straight from a demuxer; it
 * provides a monotonic system clock as the pipeline clock, which the host follows. No ALSA, DSP,
 * reference clock or audio-info context: both ends of the link are ours, and nothing in the
 * replaced graph asks for the context. RTK_REL is the release: 19, 20 or 21. */
#include <gst/gst.h>
#include <string.h>
#include <syslog.h>
#include "props.h"
#include "essink.h"

GST_DEBUG_CATEGORY_STATIC(rtkalsa_debug);
#define GST_CAT_DEFAULT rtkalsa_debug

#ifndef RTK_REL
#error "RTK_REL: 19, 20 or 21"
#endif

#if RTK_REL >= 20
#define CHANNELS "{ (int)2, (int)6 }"
#else
#define CHANNELS "2"
#endif
/* The stock template (gst-inspect-1.0 of the stock libgstrtkalsa.so) */
#define RTKALSA_SINK_CAPS                                                                          \
    "audio/x-raw, format=(string)S16LE, layout=(string)interleaved, "                              \
    "rate=(int)48000, channels=(int)" CHANNELS "; audio/x-ac3, framed=(boolean)true; "             \
    "audio/x-eac3, framed=(boolean)true; audio/x-dts, framed=(boolean)true, "                      \
    "block-size=(int){ 512, 1024, 2048 }; audio/mpeg, mpegversion=(int)1, "                        \
    "mpegaudioversion=(int)[ 1, 2 ], parsed=(boolean)true"

/* GstRtkAudioBaseSink's slave-method enum (libgstrtkaudio's, if it is loaded already) */
static GType slave_method_get_type(void)
{
    static gsize type;
    static const GEnumValue values[] = {
        {0, "GST_RTK_AUDIO_BASE_SINK_SLAVE_RESAMPLE", "resample"},
        {1, "GST_RTK_AUDIO_BASE_SINK_SLAVE_SKEW", "skew"},
        {2, "GST_RTK_AUDIO_BASE_SINK_SLAVE_NONE", "none"},
        {0, NULL, NULL},
    };
    if (g_once_init_enter(&type)) {
        GType t = g_type_from_name("GstRtkAudioBaseSinkSlaveMethod");
        g_once_init_leave(&type,
                          t ? t : g_enum_register_static("GstRtkAudioBaseSinkSlaveMethod", values));
    }
    return type;
}

#define R G_PARAM_READABLE
#define W G_PARAM_WRITABLE
#include "rtkalsaprops.h"
#define N_PROPS G_N_ELEMENTS(props)

typedef struct {
    EsSink parent;
    GValue values[N_PROPS];
} GstRtkAlsaSink;
typedef struct {
    EsSinkClass parent_class;
    void (*audio_underrun)(GstRtkAlsaSink *self);
} GstRtkAlsaSinkClass;

static GType gst_rtk_alsa_sink_get_type(void);
G_DEFINE_TYPE(GstRtkAlsaSink, gst_rtk_alsa_sink, es_sink_get_type())

static void gst_rtk_alsa_sink_set_property(GObject *obj, guint id, const GValue *value,
                                           GParamSpec *spec)
{
    GstRtkAlsaSink *self = (GstRtkAlsaSink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(value, &self->values[id - 1]);
    GST_OBJECT_UNLOCK(self);
    if (!strcmp(spec->name, "resource-info"))
        es_sink_resource_info(ES_SINK(self), value, "audio-port");
}

static void gst_rtk_alsa_sink_get_property(GObject *obj, guint id, GValue *value, GParamSpec *spec)
{
    GstRtkAlsaSink *self = (GstRtkAlsaSink *)obj;
    if (id < 1 || id > N_PROPS) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        return;
    }
    if (!strcmp(spec->name, "current-pts")) {
        g_value_set_uint64(value, es_sink_position(ES_SINK(self)));
        return;
    }
    GST_OBJECT_LOCK(self);
    g_value_copy(&self->values[id - 1], value);
    GST_OBJECT_UNLOCK(self);
}

static void gst_rtk_alsa_sink_finalize(GObject *obj)
{
    props_clear(((GstRtkAlsaSink *)obj)->values, N_PROPS);
    G_OBJECT_CLASS(gst_rtk_alsa_sink_parent_class)->finalize(obj);
}

static void gst_rtk_alsa_sink_class_init(GstRtkAlsaSinkClass *klass)
{
    GObjectClass *gobject = G_OBJECT_CLASS(klass);
    GstElementClass *element = GST_ELEMENT_CLASS(klass);
    gobject->set_property = gst_rtk_alsa_sink_set_property;
    gobject->get_property = gst_rtk_alsa_sink_get_property;
    gobject->finalize = gst_rtk_alsa_sink_finalize;
    props_install(gobject, props, N_PROPS);
    klass->parent_class.kind = WEBOS_MEDIA_AUDIO;
    g_signal_new("audio-underrun", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                 G_STRUCT_OFFSET(GstRtkAlsaSinkClass, audio_underrun), NULL, NULL,
                 g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
    gst_element_class_add_pad_template(
        element, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                      gst_caps_from_string(RTKALSA_SINK_CAPS)));
    gst_element_class_set_static_metadata(element, "Realtek ALSA sink", "Sink/Audio",
                                          "Realtek SoC audiosink",
                                          "luke.lin <luke.lin@realtek.com>");
}

static void gst_rtk_alsa_sink_init(GstRtkAlsaSink *self)
{
    props_init(self->values, props, N_PROPS);
    /* The pipeline clock, as the stock sink provides the DSP's */
    es_sink_provide_clock(ES_SINK(self), "rtkalsasink-clock");
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(rtkalsa_debug, "rtkalsasink", 0, "Realtek ALSA sink (emulator)");
    return gst_element_register(plugin, "rtkalsasink", GST_RANK_PRIMARY,
                                gst_rtk_alsa_sink_get_type());
}

#define PACKAGE "beanjar"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, rtkalsa,
                  "Realtek ALSA Plugin for the emulator", plugin_init, "1.0.0", "LGPL", PACKAGE,
                  "Beanjar")
