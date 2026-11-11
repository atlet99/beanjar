// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNC_VIDEO_H
#define VNC_VIDEO_H
/* beanjar-vnc's --video-local: the composed picture (the RFB clients' frames) as H.264 on an
 * owner-only (0600) unix socket, for this host's own front ends (beanjar vnc video-pipe, the web
 * viewer). A client sends one request line: "h264" and optionally "bitrate=N" (kbit/s, the most
 * it may take: 100 to 50000; default 10000). It gets a 32-byte header, then the stream, all
 * little-endian:
 *
 *   header  "BJVI", u32 32 (its length), u16 width, u16 height, u16 fps (the most a second),
 *           u16 codec (1: H.264), u32 bitrate (kbit/s), char encoder[12] ("nvenc", "vaapi",
 *           "v4l2", "x264"; NUL-padded)
 *   unit    u32 length, u32 flags (1: keyframe), u64 timestamp (us since beanjar-vnc started),
 *           then that many bytes: one access unit, Annex-B, SPS and PPS before every keyframe
 *
 * The encoder is open only while a client is connected, and runs on a thread of its own. It
 * serves every client (the latest request's bitrate applies to all); a new client's stream
 * starts with a keyframe forced at once. Frames are encoded as the picture changes, up to --fps,
 * and at least once a second. A client too slow to take a unit loses it and every unit up to the
 * next keyframe, forced for it once it reads again, so its stream stays decodable; one that takes
 * nothing for 3 s is disconnected. The main loop only copies each frame, or with GBM frames only
 * references it. docs/dev/architecture/vnc.md, "Video". */
#include "frame.h"
struct video_server;
/* The encoders --video-encoder names: auto tries nvenc, vaapi (with a render node), v4l2 (a V4L2
 * stateful H.264 encoder taking RGB, h264v4l2.h), x264. */
int video_encoder_known(const char *name);
/* For --video-encoder vaapi on WSL (/dev/dxg; Mesa's d3d12 VA driver over vgem's render node): the
 * VA-API device opened now, kept for the process, with LIBVA_DRIVER_NAME and GALLIUM_DRIVER set to
 * d3d12 (unless set already) and MESA_D3D12_DEFAULT_ADAPTER_NAME to adapter (unless NULL), then
 * those variables unset again: beanjar-vnc's own GL is chosen after it (main.c gl_driver,
 * --gl-adapter), so each has its own adapter. Call it before EGL and before any thread. Elsewhere
 * it does nothing: the device is opened with the encoder. 0, or -1 with the reason printed. */
int video_vaapi_prepare(const char *adapter);
/* Listens on path (an earlier run's file is removed first); NULL with errno on failure. gbm (any
 * of them: its geometry): the frames come as GBM frames (video_server_gbm_frame), NULL: as RGBX
 * bytes (video_server_frame). nv12: beanjar-vnc converts RGB to NV12 on the GPU (yuv.h), so the
 * encoders that take YUV (x264, VA-API) are opened for NV12 and get it (video_server_nv12_get);
 * 0: they convert with swscale, as frames that come as RGB anyway are. wake is called on the main
 * loop when a client starts streaming (a fresh frame is wanted) and when the last one has gone
 * (frames are no longer wanted). */
struct video_server *video_server_new(const char *path, const char *encoder, int width, int height,
                                      int fps, const struct vnc_frame *gbm, int nv12,
                                      void (*wake)(void *), void *data);
/* Whether a client streams and the encoder takes RGB (video_server_frame): NVENC, V4L2, x264 or
 * VA-API on swscale, or none open yet. Main loop. */
int video_server_rgb_wanted(struct video_server *s);
/* An NV12 picture for the encoder: height rows of Y, then height / 2 rows of U, V pairs, stride
 * bytes each (yuv_stride(width)), width x height the encoder's (the picture's, made even). */
struct video_nv12 {
    unsigned char *data;
    int width, height, stride;
    void *ref;
};
/* 1 with a buffer in *p to fill, when the open encoder takes NV12 from the GPU; 0: it takes RGB
 * (video_server_frame, video_server_gbm_frame), or none is open. Main loop. */
int video_server_nv12_get(struct video_server *s, struct video_nv12 *p);
/* *p filled: handed to the encoder as the latest frame. Not (the conversion failed): let go, and
 * the encoders opened from now on take RGB, converted with swscale. Main loop. */
void video_server_nv12_put(struct video_server *s, struct video_nv12 *p, int filled);
/* Whether any client streams (or is about to): frames are wanted. Any thread. */
int video_server_wanted(struct video_server *s);
/* A composed frame (RGBX bytes, width*4 a row), copied for the encoder. Main loop. */
void video_server_frame(struct video_server *s, const void *rgbx);
/* A composed GBM frame (frame.h), complete: referenced, not copied, until the encoder is done with
 * it (or a newer one replaces it). Main loop. */
void video_server_gbm_frame(struct video_server *s, struct vnc_frame *f);
/* New pictures encoded so far and the encoder thread's time on them, in us (any thread): the
 * frame rate's measure (rate.h). */
void video_server_encoded(struct video_server *s, unsigned long long *frames,
                          unsigned long long *us);
/* The most frames a second from now on: the header of the clients that come later, and the
 * encoders opened later (main loop). */
void video_server_set_fps(struct video_server *s, int fps);
/* The units sent so far (any thread). */
unsigned long long video_server_units(struct video_server *s);
/* Ends the encoder thread, closes the clients, removes the socket, prints the counters. */
void video_server_free(struct video_server *s);
#endif
