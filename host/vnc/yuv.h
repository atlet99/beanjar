// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VNC_YUV_H
#define VNC_YUV_H
/* beanjar-vnc's RGB to YUV 4:2:0 on the GPU (docs/dev/architecture/vnc.md, "Video"), for the
 * encoders that take YUV (x264, VA-API): one GLES 2 pass over the composed picture's texture writes
 * NV12, four bytes to each pixel of an RGBA target, read back once (1.5 bytes a pixel, no
 * conversion on the CPU). The conversion is swscale's that it replaces: BT.601 limited range,
 * chroma the mean of two columns over four rows weighted 1, 3, 3, 1 (SWS_BILINEAR's siting), the
 * edges repeated. */

/* Bytes a row of the NV12 picture yuv_convert writes for width: a multiple of 4. */
static inline int yuv_stride(int width)
{
    return (width + 3) & ~3;
}
struct yuv;
/* The shader compiled and checked on a small known picture against the same conversion done
 * here on the CPU; NULL with the reason in *why where it does not compile, or the context has no
 * high precision floats, or it gives other values. The GL context must be current. */
struct yuv *yuv_new(const char **why);
/* The top-left width x height (both even) of the texture tex (tex_w x tex_h texels, its top row
 * first in memory) as NV12 in nv12: height rows of Y, then height / 2 rows of U, V pairs, each
 * yuv_stride(width) bytes. Waits for the GPU. Leaves the framebuffer binding and the program as
 * they were. 0, or -1 on a GL error. */
int yuv_convert(struct yuv *y, unsigned tex, int tex_w, int tex_h, int width, int height,
                unsigned char *nv12);
/* GL context current. */
void yuv_free(struct yuv *y);
#endif
