// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The owned decoder must preserve elementary bytes and timing for the host. */
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <string.h>

static void check(const char *factory, const char *caps_text)
{
    GstElement *pipeline = gst_pipeline_new(NULL);
    GstElement *source = gst_element_factory_make("appsrc", NULL);
    GstElement *decoder = gst_element_factory_make(factory, NULL);
    GstElement *sink = gst_element_factory_make("appsink", NULL);
    g_assert_nonnull(decoder);
    GstCaps *caps = gst_caps_from_string(caps_text);
    g_object_set(source, "caps", caps, "format", GST_FORMAT_TIME, NULL);
    g_object_set(sink, "sync", FALSE, NULL);
    gst_caps_unref(caps);
    gst_bin_add_many(GST_BIN(pipeline), source, decoder, sink, NULL);
    g_assert_true(gst_element_link_many(source, decoder, sink, NULL));
    g_assert_cmpint(gst_element_set_state(pipeline, GST_STATE_PLAYING), !=,
                    GST_STATE_CHANGE_FAILURE);
    /* This is a transport payload, not a claim of decoding this short sample. */
    const guint8 bytes[] = {0, 0, 0, 1, 0x65, 0x88, 0x99, 0xaa};
    GstBuffer *buffer = gst_buffer_new_allocate(NULL, sizeof bytes, NULL);
    gst_buffer_fill(buffer, 0, bytes, sizeof bytes);
    GST_BUFFER_PTS(buffer) = 123 * GST_MSECOND;
    GST_BUFFER_DTS(buffer) = 100 * GST_MSECOND;
    GST_BUFFER_DURATION(buffer) = 40 * GST_MSECOND;
    g_assert_cmpint(gst_app_src_push_buffer(GST_APP_SRC(source), buffer), ==, GST_FLOW_OK);
    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 2 * GST_SECOND);
    g_assert_nonnull(sample);
    buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    g_assert_true(gst_buffer_map(buffer, &map, GST_MAP_READ));
    g_assert_cmpuint(map.size, ==, sizeof bytes);
    g_assert_cmpint(memcmp(map.data, bytes, sizeof bytes), ==, 0);
    g_assert_cmpuint(GST_BUFFER_PTS(buffer), ==, 123 * GST_MSECOND);
    g_assert_cmpuint(GST_BUFFER_DTS(buffer), ==, 100 * GST_MSECOND);
    g_assert_cmpuint(GST_BUFFER_DURATION(buffer), ==, 40 * GST_MSECOND);
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

int main(int argc, char **argv)
{
    gst_init(&argc, &argv);
    check("lxvideodec", "video/x-h264,stream-format=byte-stream,alignment=au,width=640,height=360");
    check("aac_audiodec", "audio/mpeg,mpegversion=4,stream-format=adts,rate=48000,channels=2");
    return 0;
}
