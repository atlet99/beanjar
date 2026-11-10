// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include "ipc.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int64_t hm_mono_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
void hm_close_fds(int *fds, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        if (fds[i] >= 0) {
            close(fds[i]);
            fds[i] = -1;
        }
}
int hm_send(int fd, const struct host_media_message *m, const int *fds, unsigned nfds)
{
    if (nfds > HOST_MEDIA_OBJECTS) {
        errno = EINVAL;
        return -1;
    }
    char ancillary[CMSG_SPACE(sizeof(int) * HOST_MEDIA_OBJECTS)] = {0};
    struct iovec iov = {(void *)m, sizeof *m};
    struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1};
    if (nfds) {
        msg.msg_control = ancillary;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * nfds);
        struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int) * nfds);
        memcpy(CMSG_DATA(c), fds, sizeof(int) * nfds);
    }
    ssize_t n = sendmsg(fd, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return 0;
    return n == sizeof *m ? 1 : -1;
}
int hm_receive(int fd, struct host_media_message *m, int fds[HOST_MEDIA_OBJECTS], unsigned *nfds)
{
    char ancillary[CMSG_SPACE(sizeof(int) * HOST_MEDIA_OBJECTS)] = {0};
    struct iovec iov = {m, sizeof *m};
    struct msghdr msg = {.msg_iov = &iov,
                         .msg_iovlen = 1,
                         .msg_control = ancillary,
                         .msg_controllen = sizeof ancillary};
    *nfds = 0;
    for (unsigned i = 0; i < HOST_MEDIA_OBJECTS; i++)
        fds[i] = -1;
    ssize_t n = recvmsg(fd, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return 0;
    int bad = n != sizeof *m || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC));
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS ||
            c->cmsg_len < CMSG_LEN(0)) {
            bad = 1;
            continue;
        }
        size_t bytes = c->cmsg_len - CMSG_LEN(0);
        if (bytes % sizeof(int))
            bad = 1;
        for (size_t i = 0; i < bytes / sizeof(int); i++) {
            int received;
            memcpy(&received, (char *)CMSG_DATA(c) + i * sizeof(int), sizeof received);
            if (*nfds < HOST_MEDIA_OBJECTS)
                fds[(*nfds)++] = received;
            else {
                close(received);
                bad = 1;
            }
        }
    }
    if (bad || m->magic != HOST_MEDIA_MAGIC || m->version != HOST_MEDIA_VERSION ||
        m->type < HM_HELLO || m->type > HM_IMPORT_FAILED) {
        hm_close_fds(fds, *nfds);
        *nfds = 0;
        errno = EPROTO;
        return -1;
    }
    return 1;
}
int hm_validate_frame(const struct host_media_message *m, unsigned nfds)
{
    if (!m->token || !m->generation || !m->objects || m->objects != nfds ||
        nfds > HOST_MEDIA_OBJECTS)
        return 0;
    uint64_t bytes = 0;
    for (unsigned i = 0; i < nfds; i++) {
        if (!m->object[i].size || m->object[i].size > HOST_MEDIA_MAX_BYTES)
            return 0;
        bytes += m->object[i].size;
    }
    if (bytes > HOST_MEDIA_MAX_BYTES)
        return 0;
    if (m->type == HM_PCM)
        return m->format == HM_FLOAT_STEREO && nfds == 1 && m->samples > 0 &&
               m->samples <= HOST_MEDIA_MAX_BYTES / 8 &&
               m->object[0].size == (uint64_t)m->samples * 8;
    if (m->type != HM_VIDEO || !m->width || !m->height || m->width > HOST_MEDIA_MAX_DIMENSION ||
        m->height > HOST_MEDIA_MAX_DIMENSION || !m->layers || m->layers > HOST_MEDIA_LAYERS)
        return 0;
    if (m->format < HM_YUV420 || m->format > HM_DRM)
        return 0;
    for (unsigned l = 0; l < m->layers; l++) {
        if (!m->layer[l].planes || m->layer[l].planes > HOST_MEDIA_PLANES)
            return 0;
        for (unsigned p = 0; p < m->layer[l].planes; p++) {
            const struct host_media_plane *plane = &m->layer[l].plane[p];
            if (plane->object >= nfds || !plane->stride || plane->stride > HOST_MEDIA_MAX_BYTES ||
                plane->offset >= m->object[plane->object].size)
                return 0;
        }
    }
    if (m->format != HM_DRM) {
        unsigned planes = m->format == HM_NV12 ? 2 : 3;
        if (nfds != 1 || m->layers != 1 || m->layer[0].planes != planes)
            return 0;
        for (unsigned p = 0; p < planes; p++) {
            const struct host_media_plane *q = &m->layer[0].plane[p];
            unsigned rows = p && (m->format == HM_YUV420 || m->format == HM_NV12)
                                ? (m->height + 1) / 2
                                : m->height;
            unsigned cols = p && m->format != HM_YUV444 ? (m->width + 1) / 2 : m->width;
            if (p && m->format == HM_NV12)
                cols *= 2;
            if (q->stride < cols || q->offset + (uint64_t)q->stride * rows > m->object[0].size)
                return 0;
        }
    }
    return 1;
}
void hm_buffer_discard(struct hm_buffer *buffer)
{
    int saved_errno = errno;
    if (buffer->data)
        munmap(buffer->data, buffer->size);
    if (buffer->fd >= 0)
        close(buffer->fd);
    *buffer = (struct hm_buffer){.fd = -1};
    errno = saved_errno;
}
int hm_buffer_create(struct hm_buffer *buffer, size_t bytes)
{
    *buffer = (struct hm_buffer){.fd = -1};
    if (!bytes || bytes > HOST_MEDIA_MAX_BYTES) {
        errno = EINVAL;
        return -1;
    }
    buffer->fd = memfd_create("beanjar-media", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (buffer->fd < 0)
        return -1;
    if (ftruncate(buffer->fd, (off_t)bytes)) {
        hm_buffer_discard(buffer);
        return -1;
    }
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, buffer->fd, 0);
    if (p == MAP_FAILED) {
        hm_buffer_discard(buffer);
        return -1;
    }
    buffer->data = p;
    buffer->size = bytes;
    return 0;
}
int hm_buffer_finish(struct hm_buffer *buffer)
{
    if (buffer->fd < 0 || !buffer->data || !buffer->size) {
        errno = EINVAL;
        hm_buffer_discard(buffer);
        return -1;
    }
    if (munmap(buffer->data, buffer->size)) {
        hm_buffer_discard(buffer);
        return -1;
    }
    buffer->data = NULL;
    if (fcntl(buffer->fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL)) {
        hm_buffer_discard(buffer);
        return -1;
    }
    int fd = buffer->fd;
    *buffer = (struct hm_buffer){.fd = -1};
    return fd;
}
int hm_memfd(const void *data, size_t bytes)
{
    struct hm_buffer buffer;
    if (hm_buffer_create(&buffer, bytes))
        return -1;
    memcpy(buffer.data, data, bytes);
    return hm_buffer_finish(&buffer);
}
unsigned char *hm_guest_message(uint16_t type, uint32_t sequence, const void *payload, size_t size,
                                size_t *len)
{
    struct webos_media_header h = {
        .magic = WEBOS_MEDIA_MAGIC,
        .type = type,
        .size = (uint32_t)size,
        .sequence = sequence,
        .mono = hm_mono_ns(),
        .clock = WEBOS_MEDIA_NONE,
    };
    if (size > WEBOS_MEDIA_MAX_PAYLOAD)
        return NULL;
    unsigned char *m = malloc(sizeof h + size);
    if (!m)
        return NULL;
    h.check = webos_media_check(&h);
    memcpy(m, &h, sizeof h);
    if (payload)
        memcpy(m + sizeof h, payload, size);
    *len = sizeof h + size;
    return m;
}

unsigned char *hm_picture(uint32_t sequence, const struct webos_media_picture *p, size_t bytes,
                          size_t *len)
{
    if (bytes > WEBOS_MEDIA_MAX_PAYLOAD - sizeof *p)
        return NULL;
    unsigned char *m =
        hm_guest_message(WEBOS_MEDIA_PICTURE, sequence, NULL, sizeof *p + bytes, len);
    if (m)
        memcpy(m + sizeof(struct webos_media_header), p, sizeof *p);
    return m;
}
