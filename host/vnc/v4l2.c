// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
/* V4L2 mem2mem helpers (v4l2.h). */
#include "v4l2.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int v4l2_xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    while ((r = ioctl(fd, req, arg)) < 0 && errno == EINTR)
        ;
    return r;
}

int v4l2_has_format(int fd, enum v4l2_buf_type type, uint32_t fourcc)
{
    for (struct v4l2_fmtdesc d = {.type = type}; !v4l2_xioctl(fd, VIDIOC_ENUM_FMT, &d); d.index++)
        if (d.pixelformat == fourcc)
            return 1;
    return 0;
}

int v4l2_find_m2m(uint32_t in, uint32_t out, char *name, size_t n)
{
    glob_t g;
    int found = -1;
    if (glob("/dev/video*", 0, NULL, &g))
        return -1;
    for (size_t i = 0; i < g.gl_pathc && found < 0; i++) {
        int fd = open(g.gl_pathv[i], O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        struct v4l2_capability cap = {0};
        uint32_t caps = 0;
        if (!v4l2_xioctl(fd, VIDIOC_QUERYCAP, &cap))
            caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities;
        if ((caps & V4L2_CAP_VIDEO_M2M_MPLANE) && (caps & V4L2_CAP_STREAMING) &&
            v4l2_has_format(fd, V4L2_OUT_MP, in) && v4l2_has_format(fd, V4L2_CAP_MP, out)) {
            found = fd;
            snprintf(name, n, "%s (%s)", g.gl_pathv[i], (char *)cap.card);
        } else
            close(fd);
    }
    globfree(&g);
    return found;
}

int v4l2_set_format(int fd, enum v4l2_buf_type type, uint32_t fourcc, int w, int h,
                    enum v4l2_colorspace cs, struct v4l2_pix_format_mplane *got)
{
    struct v4l2_format f = {.type = type};
    f.fmt.pix_mp.pixelformat = fourcc;
    f.fmt.pix_mp.width = (uint32_t)w;
    f.fmt.pix_mp.height = (uint32_t)h;
    f.fmt.pix_mp.field = V4L2_FIELD_NONE;
    f.fmt.pix_mp.colorspace = cs;
    if (v4l2_xioctl(fd, VIDIOC_S_FMT, &f) < 0)
        return -1;
    *got = f.fmt.pix_mp;
    return got->pixelformat == fourcc ? 0 : -1;
}

int v4l2_reqbufs(int fd, enum v4l2_buf_type type, enum v4l2_memory memory, unsigned count)
{
    struct v4l2_requestbuffers r = {.count = count, .type = type, .memory = memory};
    if (v4l2_xioctl(fd, VIDIOC_REQBUFS, &r) < 0)
        return -1;
    return r.count >= count ? 0 : -1;
}

int v4l2_map_buffer(int fd, enum v4l2_buf_type type, unsigned index, struct v4l2_plane_map *planes)
{
    struct v4l2_plane p[VIDEO_MAX_PLANES] = {0};
    struct v4l2_buffer b = {.type = type,
                            .memory = V4L2_MEMORY_MMAP,
                            .index = index,
                            .m.planes = p,
                            .length = VIDEO_MAX_PLANES};
    if (v4l2_xioctl(fd, VIDIOC_QUERYBUF, &b) < 0 || b.length > V4L2_MAX_PL)
        return -1;
    for (unsigned i = 0; i < b.length; i++) {
        planes[i].len = p[i].length;
        planes[i].ptr =
            mmap(NULL, p[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, p[i].m.mem_offset);
        if (planes[i].ptr == MAP_FAILED) {
            planes[i].ptr = NULL;
            return -1;
        }
    }
    return (int)b.length;
}

int v4l2_stream_on(int fd, enum v4l2_buf_type type)
{
    int t = type;
    return v4l2_xioctl(fd, VIDIOC_STREAMON, &t);
}

int v4l2_queue(int fd, enum v4l2_buf_type type, enum v4l2_memory memory, unsigned index, int planes,
               const size_t *len, const int *fds)
{
    struct v4l2_plane p[V4L2_MAX_PL] = {0};
    for (int i = 0; i < planes; i++) {
        p[i].length = (uint32_t)len[i];
        p[i].bytesused = V4L2_TYPE_IS_OUTPUT(type) ? (uint32_t)len[i] : 0;
        if (fds)
            p[i].m.fd = fds[i];
    }
    struct v4l2_buffer b = {
        .type = type, .memory = memory, .index = index, .m.planes = p, .length = (uint32_t)planes};
    return v4l2_xioctl(fd, VIDIOC_QBUF, &b);
}

long v4l2_dequeue(int fd, enum v4l2_buf_type type, enum v4l2_memory memory, uint32_t *flags)
{
    struct v4l2_plane p[V4L2_MAX_PL] = {0};
    struct v4l2_buffer b = {.type = type, .memory = memory, .m.planes = p, .length = V4L2_MAX_PL};
    for (;;) {
        if (!v4l2_xioctl(fd, VIDIOC_DQBUF, &b)) {
            if (flags)
                *flags = b.flags;
            return b.flags & V4L2_BUF_FLAG_ERROR ? -1 : (long)p[0].bytesused;
        }
        if (errno != EAGAIN)
            return -1;
        struct pollfd pfd = {.fd = fd, .events = V4L2_TYPE_IS_OUTPUT(type) ? POLLOUT : POLLIN};
        if (poll(&pfd, 1, V4L2_WAIT_MS) <= 0) {
            errno = ETIMEDOUT;
            return -1;
        }
    }
}
