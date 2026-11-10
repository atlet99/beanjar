// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The "decoder" half of a decoder + sink pair whose sink sends the stream to the host (W17H's
 * omx_lxvideodec and audio_dec; see esdec.c). */
#ifndef ESDEC_H
#define ESDEC_H
#include <gst/gst.h>
#include "mediaport.h"

#define ESDEC_API __attribute__((visibility("hidden")))

/* The field of the decoder's raw src caps that carries the compressed stream's own caps, as
 * gst_caps_to_string: the sink sends those to the host, and its buffers are the stream's. */
#define ES_CAPS_FIELD "webos-es-caps"

/* The field of the raw src caps saying each buffer is length-prefixed (es_dec_length_prefix). */
#define ES_FRAMING_FIELD "webos-es-framing"

struct es_dec {
    GstPad *sinkpad, *srcpad;
    /* each buffer as {u32 LE length; data; padding to whole PCM frames} (es_dec_length_prefix) */
    guint length_prefix;
    /* The raw caps standing for es (the src template's media type, fixed). */
    GstCaps *(*raw_caps)(GstElement *element, GstCaps *es);
    GstCaps *pending; /* output caps not pushed yet: with the next buffer */
    GList *held;      /* serialized events after them, in order, until then */
    guint64 buffers, bytes;
    /* video decoders (es_dec_report_underrun): BUFFER_UNDERRUN downstream when starved */
    gboolean report_underrun;
    GstSegment segment;
    struct es_starve *starve;
    /* Decoding for a consumer that needs real pixels (es_dec_consumer in esdec.c): video
     * decoders whose output goes to anything but the emulator's own sinks have the host decode
     * each buffer and push its NV12 picture instead of the stream. */
    gboolean video;
    gint consumer;             /* ES_CONSUMER_*: what takes the output, as last found */
    GstCaps *raw;              /* the last pass-through caps (raw_caps), to go back to */
    GstCaps *es;               /* the stream's own caps */
    GstCaps *picture_caps;     /* the decoded pictures' caps, as pushed */
    gboolean decoding;         /* the port is in a DECODE session */
    const char *format;        /* of its pictures: "NV12" or "YUY2" (choose_format) */
    GstCapsFeatures *features; /* their caps features, as downstream takes them */
    struct media_port port;
    guint32 input; /* the sequence of the buffer being answered */
    GstBuffer *in; /* and the buffer, for its metadata */
    guint in_pictures;
    GstFlowReturn last_flow;
    guint64 pictures, unanswered, dropped;
    gint64 wait_us;
    gint64 report_us; /* the last rate line (decode_input) */
    guint64 report_pictures, report_inputs;
    gint64 report_wait_us;
    gint64 send_us, report_send_us; /* of wait_us: sending the inputs */
    guint64 report_reads;
};
enum { ES_CONSUMER_UNKNOWN, ES_CONSUMER_SINK, ES_CONSUMER_PIXELS };

/* A one-shot starvation watch: armed at each buffer's running time + ES_STARVE_NS on the element's
 * clock; fires once (PLAYING, not ended) when no buffer re-armed it by then. The fields are
 * protected by the owner's object lock. */
#define ES_STARVE_NS (1000 * GST_MSECOND)
struct es_starve {
    GstElement *owner;
    void (*starved)(GstElement *owner);
    GstClockID id;
    gboolean fired, ended;
};

/* The instance layout esdec.c's pad functions expect: the state right after the GstElement. */
struct es_dec_element {
    GstElement parent;
    struct es_dec dec;
};

ESDEC_API void es_dec_init(struct es_dec_element *self, GstPadTemplate *sink, GstPadTemplate *src,
                           GstCaps *(*raw_caps)(GstElement *element, GstCaps *es));
/* The element's finalize: what is still held. */
ESDEC_API void es_dec_clear(struct es_dec_element *self);
/* Video decoders: on input starvation, send the custom downstream BUFFER_UNDERRUN event to the
 * sink as MStar's gst-omx decoders do (the sink then emits video-underrun). */
ESDEC_API void es_dec_report_underrun(struct es_dec_element *self);
/* Audio decoders whose raw caps an element on the way to the sink takes as PCM and cuts down
 * to whole frames of bpf bytes: each buffer goes as {u32 LE length; the stream's bytes; padding
 * to a multiple of bpf}, and the caps say so (ES_FRAMING_FIELD); the sink takes the stream's
 * bytes back out (es_buffer_unframe). */
ESDEC_API void es_dec_length_prefix(struct es_dec_element *self, guint bpf);
/* buf's stream bytes (a new reference): unframed when caps had ES_FRAMING_FIELD */
ESDEC_API GstBuffer *es_buffer_unframe(GstBuffer *buf);
ESDEC_API gboolean es_caps_framed(GstCaps *caps);
/* The starvation watch (struct es_starve); arm with the buffer's PTS in segment, -1: none. */
ESDEC_API struct es_starve *es_starve_new(GstElement *owner, void (*starved)(GstElement *owner));
ESDEC_API void es_starve_free(struct es_starve *w);
ESDEC_API void es_starve_arm(struct es_starve *w, const GstSegment *segment, GstClockTime pts);
ESDEC_API void es_starve_reset(struct es_starve *w, gboolean ended);
/* The stream's own caps from caps made by es_dec (NULL if caps carry none: really raw). */
ESDEC_API GstCaps *es_caps_from(GstCaps *caps);
/* An integer field of LG's resource-info structure (video-port, audio-port), whatever its type. */
ESDEC_API gboolean resource_info_int(const GstStructure *s, const char *field, gint *out);

#endif
