// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
/* One received frame: validate its FDs, map read-only software planes, and own
 * its release (returned on free). No pixels are decoded or converted here. */
#include "media-priv.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

void frame_free(struct received_frame **ptr)
{
    struct received_frame *f = *ptr;
    if (!f)
        return;
    if (f->mapping)
        munmap(f->mapping, f->descriptor.object[0].size);
    hm_close_fds(f->fds, f->descriptor.objects);
    struct host_media_message release = {.magic = HOST_MEDIA_MAGIC,
                                         .version = HOST_MEDIA_VERSION,
                                         .type = f->release_type ? f->release_type : HM_RELEASE,
                                         .generation = f->descriptor.generation,
                                         .token = f->descriptor.token};
    if (hm_send(f->connection, &release, NULL, 0) != 1)
        shutdown(f->connection, SHUT_RDWR); /* Never silently lose a lease release. */
    close(f->connection);
    free(f);
    *ptr = NULL;
}

struct received_frame *frame_receive(int fd, const struct host_media_message *msg, int *fds,
                                     unsigned nfds)
{
    if (!hm_validate_frame(msg, nfds))
        return NULL;
    if (msg->format != HM_DRM) {
        struct stat st;
        int seals = fcntl(fds[0], F_GET_SEALS);
        int required = F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
        if (fstat(fds[0], &st) || st.st_size < 0 || (uint64_t)st.st_size != msg->object[0].size ||
            seals < 0 || (seals & required) != required)
            return NULL;
    }
    struct received_frame *f = calloc(1, sizeof *f);
    if (!f)
        return NULL;
    f->connection = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (f->connection < 0) {
        free(f);
        return NULL;
    }
    f->descriptor = *msg;
    f->format = msg->format;
    f->width = msg->width;
    f->height = msg->height;
    f->colorspace = msg->colorspace;
    f->color_range = msg->color_range;
    f->pts = msg->pts;
    if (f->format != HM_DRM) {
        f->mapping = mmap(NULL, msg->object[0].size, PROT_READ, MAP_SHARED, fds[0], 0);
        if (f->mapping == MAP_FAILED) {
            close(f->connection);
            free(f);
            return NULL;
        }
        if (msg->type == HM_VIDEO)
            for (unsigned p = 0; p < msg->layer[0].planes; p++) {
                f->data[p] = (uint8_t *)f->mapping + msg->layer[0].plane[p].offset;
                f->linesize[p] = msg->layer[0].plane[p].stride;
            }
    }
    for (unsigned i = 0; i < HOST_MEDIA_OBJECTS; i++) {
        f->fds[i] = fds[i];
        fds[i] = -1;
    }
    return f;
}
