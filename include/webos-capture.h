// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef WEBOS_CAPTURE_H
#define WEBOS_CAPTURE_H
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif
/* On-demand TV readback over webos.capture. Little endian; RGBA, top row first.
 * Sequence zero is a provider connection notification (status 0 or -ENODEV).
 * A request has nonzero sequence, zero size/status; its response has the same
 * dimensions/mode and either pixels or a negative errno with no pixels.
 * There is one outstanding request; replies from expired requests are rejected. */
#define WEBOS_CAPTURE_MAGIC  0x50414357u
#define WEBOS_CAPTURE_WIDTH  1920u
#define WEBOS_CAPTURE_HEIGHT 1080u
#define WEBOS_CAPTURE_MAX    (WEBOS_CAPTURE_WIDTH * WEBOS_CAPTURE_HEIGHT * 4u)
enum webos_capture_mode {
    WEBOS_CAPTURE_VIDEO = 2,
    WEBOS_CAPTURE_BLENDED = 3,
    WEBOS_CAPTURE_OSD = 4,
};
struct webos_capture_header {
    uint32_t magic, sequence, mode, width, height, size;
    int32_t status;
    uint32_t reserved;
};
_Static_assert(sizeof(struct webos_capture_header) == 32, "capture wire header");
#endif
