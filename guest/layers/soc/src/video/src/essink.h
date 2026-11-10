// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The sink half of a decoder + sink pair whose sink sends the stream to the host (essink.c):
 * W17H's omx_lxvideosink and audio_sink, MStar's mvsink, omxaudiosink and masink. */
#ifndef ESSINK_H
#define ESSINK_H
#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "streaminfo.h"
#include "mediaport.h"

#define ESSINK_API __attribute__((visibility("hidden")))

typedef struct {
    GstBaseSink parent;
    struct media_port port;
    gint channel;         /* the decoder port (resource-info), -1 before it is known */
    GstBuffer *prerolled; /* sent from preroll(); render() gets it again */
    guint rendered;
    /* video */
    struct stream_info info;
    gchar mime[48];  /* the stream's media type ("video/x-h264") */
    gboolean posted; /* post_info done for the current caps */
    /* the frames sent and not due yet (stream time), for current-pts; the last one due */
    guint64 frames[64];
    guint head, count;
    guint64 shown_pts;
    gboolean eos; /* EOS reached the sink (until a flush or stop) */
    /* audio: audio-underrun once when no sample follows the last one due (es_starve) */
    struct es_starve *starve;
    /* audio */
    GstClock *clock; /* provided as the pipeline clock, if the element does */
    gboolean logged_caps;
    gboolean framed; /* the decoder's buffers are length-prefixed (es_dec_length_prefix) */
} EsSink;

typedef struct {
    GstBaseSinkClass parent_class;
    guint32 kind; /* WEBOS_MEDIA_VIDEO or WEBOS_MEDIA_AUDIO */
    /* Video: the stream's parameters are known (self->info, self->mime): post LG's messages. */
    void (*post_info)(EsSink *self);
    /* Audio: audio-underrun only while the element's app-type is this (NULL: always) */
    const char *underrun_app_type;
} EsSinkClass;

/* The abstract type, registered under a name of the plugin's own (each plugin carries a copy of
 * this code, and a process loads several): ESSINK_TYPE_NAME. */
ESSINK_API GType es_sink_get_type(void);
#define ES_SINK(obj)           ((EsSink *)(obj))
#define ES_SINK_GET_CLASS(obj) ((EsSinkClass *)G_OBJECT_GET_CLASS(obj))

/* The decoder port learnt from resource-info (or the element's channel property). */
ESSINK_API void es_sink_set_channel(EsSink *self, gint channel);
/* The stream time being shown or heard: for video the PTS of the latest frame whose time has come
 * (the host shows it then: the frames go out WEBOS_MEDIA_LEAD_NS early), for audio the
 * clock-based position; 0 if unknown. */
ESSINK_API guint64 es_sink_position(EsSink *self);
/* Audio: provide a monotonic system clock as the pipeline clock, as the stock sinks provide
 * theirs (the hardware's). */
ESSINK_API void es_sink_provide_clock(EsSink *self, const char *name);
/* resource-info's port field (video-port, audio-port) as the channel; logs the structure. */
ESSINK_API void es_sink_resource_info(EsSink *self, const GValue *value, const char *field);

#endif
