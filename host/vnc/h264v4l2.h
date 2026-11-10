// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNC_H264V4L2_H
#define VNC_H264V4L2_H
/* The video socket's "v4l2" H.264 encoder (video.h): a V4L2 stateful mem2mem H.264 encoder (the
 * first /dev/video* making H.264 from 32-bit RGB; RK3399's VEPU2 with the hantro-vpu DKMS module)
 * driven with plain V4L2 ioctls (v4l2.c), one frame at a time. RGB goes in as it is: RGBX bytes
 * copied into its input buffer, or a GBM frame's dma-buf (B, G, R, X) imported; the encoder
 * converts to YUV itself. CBR at the bitrate asked for (the encoder writes no filler, so a still
 * picture costs little), SPS and PPS before every keyframe, keyframes on request. */
#include "frame.h"
#include <stddef.h>

struct h264v4l2;
/* Opened for width x height frames, fps a second, bitrate kbit/s; dmabuf: GBM frames of that
 * many bytes a row (0: RGBX bytes, width * 4 a row). NULL with the reason printed. */
struct h264v4l2 *h264v4l2_open(int width, int height, int fps, int bitrate, unsigned dmabuf);
/* One frame (rgbx, or f for GBM frames), as a keyframe when key: *out and *n one access unit
 * (Annex-B), valid until the next call, *is_key whether it is one. 0, or -1 with the reason
 * printed. */
int h264v4l2_encode(struct h264v4l2 *e, const unsigned char *rgbx, const struct vnc_frame *f,
                    int key, const unsigned char **out, size_t *n, int *is_key);
/* The node and its driver's card, for the log. */
const char *h264v4l2_detail(const struct h264v4l2 *e);
void h264v4l2_close(struct h264v4l2 *e);
#endif
