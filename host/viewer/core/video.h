// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_VIDEO_H
#define VIEWER_VIDEO_H
#include "../../../include/webos-video.h"
#include <GLES2/gl2.h>
#include "input.h"
#include <stddef.h>
/* The guest's video windows from the webos.video port (tvservice's scaler windows, through
 * guest/layers/soc/h13's libhal_lg115x). Every shown context is drawn at its window, under the OSD:
 * the decoded picture of the media stream on its input (media.c, video milestone 2); while there
 * is none, black as on a TV, or with decoding off (no media, or a helper without a decoder) the
 * milestone 1 checkerboard. */
struct media;
struct video {
    int fd;
    char *path;
    size_t used;
    unsigned char buffer[2 * sizeof(struct webos_video_packet)];
    struct webos_video_packet table; /* latest frame; table.count 0 before the first */
    GLuint program;
    GLint origin, square;
    unsigned packets, changes, drawn, pictures;
    struct media *media; /* NULL: decoding off (--no-media), checkerboards only */
};
void video_init(struct video *v, const char *path);
int video_poll(struct video *v);
/* Draws the shown contexts into the OSD's viewport r (drawable pixels, GL origin at the bottom;
 * the bars around r are symmetric). Leaves blending disabled and the viewport changed. */
void video_draw(struct video *v, struct rect r);
void video_close(struct video *v);
#endif
