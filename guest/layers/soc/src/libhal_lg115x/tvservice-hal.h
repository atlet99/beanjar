// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* guest/layers/soc/src/libhal_lg115x: what the libhal_lg115x shim's parts share (hal-*.c), and what
 * it exports to the libdile_vt shim (libdile_vt/dile-vt.c). That shim looks the exports up at run
 * time (dlsym), so that a process that loads it without this library keeps LG's DILE unchanged. */
#ifndef TVSERVICE_HAL_H
#define TVSERVICE_HAL_H

#include "../../../common/hal-common/hal-common.h" /* hal_log */

/* The video windows the viewer shows (hal-video.c), for tv.capture's picture of the video plane
 * (libdile_vt): at most max, returns the count. */
struct webos_video_context;
unsigned tvservice_hal_video_shown(struct webos_video_context *out, unsigned max);

#endif
