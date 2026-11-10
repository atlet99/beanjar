// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
/* The video socket's V4L2 H.264 encoder (h264v4l2.h). */
#include "h264v4l2.h"
#include "v4l2.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct h264v4l2 {
    int fd, width, height;
    unsigned dmabuf;               /* GBM frames' bytes a row, or 0 */
    unsigned in_stride;            /* the encoder's input bytes a row */
    size_t in_size;                /* its input's bytes (sizeimage: whole macroblock rows) */
    struct v4l2_plane_map in, out; /* mapped: the input (RGBX copied in), the stream */
    char detail[96];
};

static int fail(const char *what)
{
    fprintf(stderr, "beanjar-vnc: video: v4l2 H.264: %s: %s\n", what, strerror(errno));
    return -1;
}

static void control(int fd, unsigned id, int value, const char *name)
{
    struct v4l2_control c = {.id = id, .value = value};
    if (v4l2_xioctl(fd, VIDIOC_S_CTRL, &c) < 0)
        fprintf(stderr, "beanjar-vnc: video: v4l2 H.264: %s %d: %s\n", name, value,
                strerror(errno));
}

struct h264v4l2 *h264v4l2_open(int width, int height, int fps, int bitrate, unsigned dmabuf)
{
    struct h264v4l2 *e = calloc(1, sizeof *e);
    e->width = width;
    e->height = height;
    e->dmabuf = dmabuf;
    /* GBM frames are B, G, R, X bytes (DRM XRGB8888, V4L2 XBGR32); RGBX bytes are V4L2 RGBX32 */
    uint32_t in_format = dmabuf ? V4L2_PIX_FMT_XBGR32 : V4L2_PIX_FMT_RGBX32;
    e->fd = v4l2_find_m2m(in_format, V4L2_PIX_FMT_H264, e->detail, sizeof e->detail);
    if (e->fd < 0) {
        fprintf(stderr,
                "beanjar-vnc: video: v4l2 H.264: no V4L2 H.264 encoder taking %s (a "
                "mem2mem /dev/video*)\n",
                dmabuf ? "XBGR32" : "RGBX32");
        free(e);
        return NULL;
    }
    struct v4l2_pix_format_mplane coded, raw;
    /* the coded format first (a stateful encoder's order), then the raw one at the same size */
    if (v4l2_set_format(e->fd, V4L2_CAP_MP, V4L2_PIX_FMT_H264, width, height,
                        V4L2_COLORSPACE_DEFAULT, &coded) < 0 ||
        v4l2_set_format(e->fd, V4L2_OUT_MP, in_format, width, height, V4L2_COLORSPACE_SRGB, &raw) <
            0) {
        fail("the formats");
        goto fail;
    }
    e->in_stride = raw.plane_fmt[0].bytesperline;
    e->in_size = raw.plane_fmt[0].sizeimage;
    if ((int)raw.width != width || (int)raw.height != height || raw.num_planes != 1 ||
        (int)coded.width != width || (int)coded.height != height ||
        (dmabuf && e->in_stride != dmabuf) || (!dmabuf && e->in_stride < (unsigned)width * 4)) {
        fprintf(stderr,
                "beanjar-vnc: video: v4l2 H.264: %s takes no %dx%d %s of %u bytes a row (it "
                "offers %ux%u, %u bytes a row)\n",
                e->detail, width, height, dmabuf ? "XBGR32" : "RGBX32",
                dmabuf ? dmabuf : (unsigned)width * 4, raw.width, raw.height, e->in_stride);
        goto fail;
    }
    struct v4l2_streamparm parm = {.type = V4L2_OUT_MP};
    parm.parm.output.timeperframe = (struct v4l2_fract){1, (unsigned)fps};
    v4l2_xioctl(e->fd, VIDIOC_S_PARM, &parm);
    control(e->fd, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, V4L2_MPEG_VIDEO_BITRATE_MODE_CBR,
            "bitrate mode");
    control(e->fd, V4L2_CID_MPEG_VIDEO_BITRATE, bitrate * 1000, "bitrate");
    control(e->fd, V4L2_CID_MPEG_VIDEO_GOP_SIZE, fps * 10, "GOP size"); /* keyframes on request */
    if (v4l2_reqbufs(e->fd, V4L2_OUT_MP, dmabuf ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP, 1) < 0 ||
        v4l2_reqbufs(e->fd, V4L2_CAP_MP, V4L2_MEMORY_MMAP, 1) < 0) {
        fail("the buffers");
        goto fail;
    }
    if ((!dmabuf && v4l2_map_buffer(e->fd, V4L2_OUT_MP, 0, &e->in) != 1) ||
        v4l2_map_buffer(e->fd, V4L2_CAP_MP, 0, &e->out) != 1) {
        fail("mapping the buffers");
        goto fail;
    }
    if (v4l2_stream_on(e->fd, V4L2_OUT_MP) < 0 || v4l2_stream_on(e->fd, V4L2_CAP_MP) < 0) {
        fail("starting");
        goto fail;
    }
    return e;
fail:
    h264v4l2_close(e);
    return NULL;
}

int h264v4l2_encode(struct h264v4l2 *e, const unsigned char *rgbx, const struct vnc_frame *f,
                    int key, const unsigned char **out, size_t *n, int *is_key)
{
    if (!f) {
        unsigned char *dst = e->in.ptr;
        for (int y = 0; y < e->height; y++)
            memcpy(dst + (size_t)y * e->in_stride, rgbx + (size_t)y * e->width * 4,
                   (size_t)e->width * 4);
    }
    if (key)
        control(e->fd, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1, "forcing a keyframe");
    enum v4l2_memory memory = f ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    size_t out_len = e->out.len;
    if (v4l2_queue(e->fd, V4L2_OUT_MP, memory, 0, 1, &e->in_size, f ? &f->fd : NULL) < 0 ||
        v4l2_queue(e->fd, V4L2_CAP_MP, V4L2_MEMORY_MMAP, 0, 1, &out_len, NULL) < 0)
        return fail("queueing");
    uint32_t flags = 0;
    long got = v4l2_dequeue(e->fd, V4L2_CAP_MP, V4L2_MEMORY_MMAP, &flags);
    if (got <= 0 || v4l2_dequeue(e->fd, V4L2_OUT_MP, memory, NULL) < 0)
        return fail("encoding");
    *out = e->out.ptr;
    *n = (size_t)got;
    *is_key = !!(flags & V4L2_BUF_FLAG_KEYFRAME);
    return 0;
}

const char *h264v4l2_detail(const struct h264v4l2 *e)
{
    return e->detail;
}

void h264v4l2_close(struct h264v4l2 *e)
{
    if (!e)
        return;
    if (e->in.ptr)
        munmap(e->in.ptr, e->in.len);
    if (e->out.ptr)
        munmap(e->out.ptr, e->out.len);
    if (e->fd >= 0)
        close(e->fd);
    free(e);
}
