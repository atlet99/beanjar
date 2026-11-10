// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef CURSOR_CHANNEL_H
#define CURSOR_CHANNEL_H
#include <stdint.h>
void cursor_channel_image(const void *bgra, unsigned width, unsigned height, unsigned pitch);
void cursor_channel_visible(int visible);
void cursor_channel_move(const void *dile_position);
#endif
