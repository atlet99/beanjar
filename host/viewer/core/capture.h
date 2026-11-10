// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_CAPTURE_H
#define VIEWER_CAPTURE_H
#include "../../../include/webos-capture.h"
#include <stddef.h>
#include <stdint.h>
typedef int (*capture_render)(void *, const struct webos_capture_header *, unsigned char **);
struct capture {
    int fd;
    char *path;
    int64_t retry, next_hello;
    unsigned char request[sizeof(struct webos_capture_header)];
    size_t used, length, sent;
    unsigned char *response;
};
void capture_init(struct capture *, const char *);
void capture_poll(struct capture *, capture_render, void *);
void capture_close(struct capture *);
#endif
