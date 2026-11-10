// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* "rvscale": Realtek's GstRvScale (libgstrvscale.so, K8: W22H) for the emulator.
 *
 * The stock element is a GstVideoFilter that scales NV12 on the SoC's scaler engine (KADP's
 * omx_scale_engine_stretchblt, between dma-bufs of Realtek's DMA heap). Its instance init opens
 * that engine, whose se_open asserts (se_drv.c:167) where /dev/se0 cannot be mapped: the camera
 * player's PTZ pipeline (libStarfishCameraPlayer's CameraDecodePlayer, on K8HP) appsrc !
 * omxmjpegdec ... ! capsfilter caps="video/x-raw, format=NV12" ! videoconvert ! videocrop
 * name=dec_crop ! rvscale name=scale ! capsfilter caps="video/x-raw(memory:DMABuf), width=W,
 * height=H" ! appsink aborted WebAppMgr's video capture process before the first frame.
 *
 * Here: the stock pad templates, metadata and rank (NONE, the stock plugin_init's), the scaling in
 * software. At equal sizes (the camera's preview: the decoder's size is the one asked for, PTZ not
 * zoomed) the buffer passes through untouched, a host-decoded picture's shared slot included
 * (mediaport.c); a crop that changes the size is scaled bilinearly into system memory. The output's
 * memory:DMABuf is only a caps feature: the camera player's appsink consumer maps each buffer
 * (gst_buffer_map; no gst_dmabuf_memory_get_fd or EGL import in libStarfishCameraPlayer), so no
 * dma-buf is made. It takes the planes from the buffer's GstVideoMeta (it has no CbCr pointer
 * without one), so a scaled picture gets one; the input's planes are its GstVideoMeta's, or
 * GStreamer's default NV12 layout (stride RU4(width), CbCr at stride * RU2(height)). The stock
 * "acquire-dma-buffer-pool" signal is not provided: nothing in the firmware emits it on this
 * element. */
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <string.h>
#include <syslog.h>

GST_DEBUG_CATEGORY_STATIC(rvscale_debug);
#define GST_CAT_DEFAULT rvscale_debug

/* The stock template (libgstrvscale.so's string; both pads) */
#define RVSCALE_CAPS                                                                               \
    "video/x-raw(memory:DMABuf), format = (string) { NV12 }, width = (int) [ 2, 8192 ], "          \
    "height = (int) [ 2, 8192 ], framerate = (fraction) [ 0, 60 ];video/x-raw, format = (string) " \
    "{ NV12 }, width = (int) [ 2, 8192 ], height = (int) [ 2, 8192 ], framerate = (fraction) [ "   \
    "0, "                                                                                          \
    "60 ]"

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(RVSCALE_CAPS));
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(RVSCALE_CAPS));

typedef struct {
    GstBaseTransform parent;
    gint in_w, in_h, out_w, out_h;
    guint64 frames, scaled;
} GstRvScale;
typedef struct {
    GstBaseTransformClass parent_class;
} GstRvScaleClass;

GType gst_rvscale_get_type(void);
G_DEFINE_TYPE(GstRvScale, gst_rvscale, GST_TYPE_BASE_TRANSFORM)

/* libgstvideo's, declared here (the soc layer compiles against GStreamer core's headers only),
 * with GstVideoMeta's public fields as GStreamer 1.x lays them out. NV12 is GstVideoFormat 23. */
struct video_meta {
    GstMeta meta;
    GstBuffer *buffer;
    int flags, format, id;
    guint width, height, n_planes;
    gsize offset[4];
    gint stride[4];
};
extern struct video_meta *gst_buffer_get_video_meta(GstBuffer *buffer);
extern struct video_meta *gst_buffer_add_video_meta_full(GstBuffer *buffer, int flags, int format,
                                                         guint width, guint height, guint n_planes,
                                                         gsize offset[4], gint stride[4]);

#define RU2(x) (((x) + 1) & ~1)
#define RU4(x) (((x) + 3) & ~3)

static gsize nv12_size(gint w, gint h)
{
    return (gsize)RU4(w) * RU2(h) + (gsize)RU4(w) * (RU2(h) / 2);
}

/* Any size, either memory, as the stock element converts: the other caps with width and height
 * free, in both features. */
static GstCaps *transform_caps(GstBaseTransform *bt, GstPadDirection dir, GstCaps *caps,
                               GstCaps *filter)
{
    GstCaps *out = gst_caps_new_empty();
    for (guint i = 0; i < gst_caps_get_size(caps); i++) {
        GstStructure *s = gst_structure_copy(gst_caps_get_structure(caps, i));
        gst_structure_set(s, "width", GST_TYPE_INT_RANGE, 2, 8192, "height", GST_TYPE_INT_RANGE, 2,
                          8192, NULL);
        gst_structure_remove_field(s, "pixel-aspect-ratio");
        for (int f = 0; f < 2; f++) {
            GstCapsFeatures *features =
                f ? gst_caps_features_new("memory:DMABuf", NULL) : gst_caps_features_new_empty();
            out = gst_caps_merge_structure_full(out, gst_structure_copy(s), features);
        }
        gst_structure_free(s);
    }
    if (filter) {
        GstCaps *t = gst_caps_intersect_full(filter, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = t;
    }
    return out;
}

/* The other side's size where it can be (no scaling: passthrough). */
static GstCaps *fixate_caps(GstBaseTransform *bt, GstPadDirection dir, GstCaps *caps,
                            GstCaps *othercaps)
{
    gint w = 0, h = 0;
    GstStructure *from = gst_caps_get_structure(caps, 0);
    othercaps = gst_caps_truncate(othercaps);
    othercaps = gst_caps_make_writable(othercaps);
    GstStructure *to = gst_caps_get_structure(othercaps, 0);
    if (gst_structure_get_int(from, "width", &w) && gst_structure_get_int(from, "height", &h)) {
        gst_structure_fixate_field_nearest_int(to, "width", w);
        gst_structure_fixate_field_nearest_int(to, "height", h);
    }
    return gst_caps_fixate(othercaps);
}

static gboolean set_caps(GstBaseTransform *bt, GstCaps *in, GstCaps *out)
{
    GstRvScale *self = (GstRvScale *)bt;
    GstStructure *si = gst_caps_get_structure(in, 0), *so = gst_caps_get_structure(out, 0);
    if (!gst_structure_get_int(si, "width", &self->in_w) ||
        !gst_structure_get_int(si, "height", &self->in_h) ||
        !gst_structure_get_int(so, "width", &self->out_w) ||
        !gst_structure_get_int(so, "height", &self->out_h))
        return FALSE;
    gboolean same = self->in_w == self->out_w && self->in_h == self->out_h;
    gst_base_transform_set_passthrough(bt, same);
    syslog(LOG_INFO, "%s: %dx%d -> %dx%d (%s)", GST_OBJECT_NAME(bt), self->in_w, self->in_h,
           self->out_w, self->out_h, same ? "passthrough" : "scaled in software");
    return TRUE;
}

static gboolean get_unit_size(GstBaseTransform *bt, GstCaps *caps, gsize *size)
{
    gint w, h;
    GstStructure *s = gst_caps_get_structure(caps, 0);
    if (!gst_structure_get_int(s, "width", &w) || !gst_structure_get_int(s, "height", &h))
        return FALSE;
    *size = nv12_size(w, h);
    return TRUE;
}

/* One plane of 1 (Y) or 2 (CbCr, interleaved) bytes per sample, bilinear, 16.16 fixed point. */
static void scale_plane(const guint8 *src, gint sw, gint sh, gint sstride, guint8 *dst, gint dw,
                        gint dh, gint dstride, gint bpp)
{
    gint64 xstep = ((gint64)sw << 16) / dw, ystep = ((gint64)sh << 16) / dh;
    for (gint y = 0; y < dh; y++) {
        gint64 fy = y * ystep + ystep / 2 - 0x8000;
        if (fy < 0)
            fy = 0;
        gint y0 = (gint)(fy >> 16), y1 = MIN(y0 + 1, sh - 1);
        guint wy = (guint)(fy & 0xffff) >> 8;
        const guint8 *r0 = src + (gsize)y0 * sstride, *r1 = src + (gsize)y1 * sstride;
        guint8 *o = dst + (gsize)y * dstride;
        for (gint x = 0; x < dw; x++) {
            gint64 fx = x * xstep + xstep / 2 - 0x8000;
            if (fx < 0)
                fx = 0;
            gint x0 = (gint)(fx >> 16), x1 = MIN(x0 + 1, sw - 1);
            guint wx = (guint)(fx & 0xffff) >> 8;
            for (gint c = 0; c < bpp; c++) {
                guint a = r0[x0 * bpp + c], b = r0[x1 * bpp + c];
                guint d = r1[x0 * bpp + c], e = r1[x1 * bpp + c];
                guint top = a * (256 - wx) + b * wx, bot = d * (256 - wx) + e * wx;
                o[x * bpp + c] = (guint8)((top * (256 - wy) + bot * wy + 32768) >> 16);
            }
        }
    }
}

static GstFlowReturn transform(GstBaseTransform *bt, GstBuffer *in, GstBuffer *out)
{
    GstRvScale *self = (GstRvScale *)bt;
    GstMapInfo mi, mo;
    if (gst_buffer_get_size(in) < nv12_size(self->in_w, self->in_h) ||
        gst_buffer_get_size(out) < nv12_size(self->out_w, self->out_h))
        return GST_FLOW_ERROR;
    if (!gst_buffer_map(in, &mi, GST_MAP_READ))
        return GST_FLOW_ERROR;
    if (!gst_buffer_map(out, &mo, GST_MAP_WRITE)) {
        gst_buffer_unmap(in, &mi);
        return GST_FLOW_ERROR;
    }
    gint ds = RU4(self->out_w);
    gsize in_off[2] = {0, (gsize)RU4(self->in_w) * RU2(self->in_h)};
    gint in_stride[2] = {RU4(self->in_w), RU4(self->in_w)};
    struct video_meta *vm = gst_buffer_get_video_meta(in);
    if (vm && vm->n_planes == 2) {
        in_off[0] = vm->offset[0];
        in_off[1] = vm->offset[1];
        in_stride[0] = vm->stride[0];
        in_stride[1] = vm->stride[1];
    }
    scale_plane(mi.data + in_off[0], self->in_w, self->in_h, in_stride[0], mo.data, self->out_w,
                self->out_h, ds, 1);
    scale_plane(mi.data + in_off[1], (self->in_w + 1) / 2, (self->in_h + 1) / 2, in_stride[1],
                mo.data + (gsize)ds * RU2(self->out_h), (self->out_w + 1) / 2,
                (self->out_h + 1) / 2, ds, 2);
    gst_buffer_unmap(out, &mo);
    gst_buffer_unmap(in, &mi);
    if (!gst_buffer_get_video_meta(out)) {
        gsize offset[4] = {0, (gsize)ds * RU2(self->out_h), 0, 0};
        gint stride[4] = {ds, ds, 0, 0};
        gst_buffer_add_video_meta_full(out, 0, 23, self->out_w, self->out_h, 2, offset, stride);
    }
    self->scaled++;
    return GST_FLOW_OK;
}

static void gst_rvscale_class_init(GstRvScaleClass *klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS(klass);
    gst_element_class_set_static_metadata(element_class, "Video Scaler",
                                          "Filter/Converter/Video/Scaler", "Convert video format",
                                          "luke.lin <luke.lin@realtek.com>");
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);
    bt_class->transform_caps = transform_caps;
    bt_class->fixate_caps = fixate_caps;
    bt_class->set_caps = set_caps;
    bt_class->get_unit_size = get_unit_size;
    bt_class->transform = transform;
    bt_class->passthrough_on_same_caps = FALSE;
}

static void gst_rvscale_init(GstRvScale *self)
{
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(self), TRUE);
}

static gboolean plugin_init(GstPlugin *plugin)
{
    GST_DEBUG_CATEGORY_INIT(rvscale_debug, "rvscale", 0, "realtek codec scale element (emulator)");
    return gst_element_register(plugin, "rvscale", GST_RANK_NONE, gst_rvscale_get_type());
}

#define PACKAGE "beanjar"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, rvscale, "realtek codec scale element",
                  plugin_init, "1.0.0", "LGPL", PACKAGE, "Beanjar")
