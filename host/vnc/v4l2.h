// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNC_V4L2_H
#define VNC_V4L2_H
/* Plain V4L2 mem2mem helpers for the video socket's H.264 encoder (h264v4l2.c) and the frame
 * path's choice (main.c): multi-planar queues, MMAP or DMABUF buffers, one frame at a time. */
#include <linux/videodev2.h>
#include <stddef.h>
#include <stdint.h>

#define V4L2_OUT_MP  V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
#define V4L2_CAP_MP  V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
#define V4L2_MAX_PL  2
#define V4L2_WAIT_MS 1000 /* a device that has not finished a frame in this long has failed */

struct v4l2_plane_map {
    void *ptr;
    size_t len;
};

int v4l2_xioctl(int fd, unsigned long req, void *arg);
int v4l2_has_format(int fd, enum v4l2_buf_type type, uint32_t fourcc);
/* The first /dev/video* that is a multi-planar mem2mem device turning in into out; its fd (name
 * the node and its driver's card), or -1. */
int v4l2_find_m2m(uint32_t in, uint32_t out, char *name, size_t n);
/* The format; 0, or -1 when refused or another. */
int v4l2_set_format(int fd, enum v4l2_buf_type type, uint32_t fourcc, int w, int h,
                    enum v4l2_colorspace cs, struct v4l2_pix_format_mplane *got);
int v4l2_reqbufs(int fd, enum v4l2_buf_type type, enum v4l2_memory memory, unsigned count);
/* Buffer index of queue type mapped, plane by plane, into planes; the number of planes, or -1. */
int v4l2_map_buffer(int fd, enum v4l2_buf_type type, unsigned index, struct v4l2_plane_map *planes);
int v4l2_stream_on(int fd, enum v4l2_buf_type type);
/* Queues buffer index: every plane of it whole (bytesused its length), or with dmabufs. */
int v4l2_queue(int fd, enum v4l2_buf_type type, enum v4l2_memory memory, unsigned index, int planes,
               const size_t *len, const int *fds);
/* Waits for a buffer queued on type to come back; its first plane's bytesused (flags: its
 * flags), or -1. */
long v4l2_dequeue(int fd, enum v4l2_buf_type type, enum v4l2_memory memory, uint32_t *flags);
#endif
