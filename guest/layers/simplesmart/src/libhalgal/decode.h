// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef HALGAL_DECODE_H
#define HALGAL_DECODE_H
#include <stddef.h>
#include <stdint.h>
/* PNG from a file (path) or memory (data, length) as straight-alpha ARGB32, width x height
 * pixels, malloc'd; NULL with the reason in why */
uint32_t *decode_png(const char *path, const void *data, size_t length, unsigned *width,
                     unsigned *height, char *why, size_t why_len);
#endif
