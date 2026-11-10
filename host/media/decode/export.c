// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Viewer output: snapshots, bounded leases, DRM_PRIME passthrough with genuine
 * download fallback, and sealed software/audio objects. */
#include "decode-priv.h"

#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/imgutils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct host_media_message snapshot(struct stream *s, unsigned type)
{
    struct host_media_message m = {.magic = HOST_MEDIA_MAGIC,
                                   .version = HOST_MEDIA_VERSION,
                                   .type = type,
                                   .generation = s->generation,
                                   .session = s->session,
                                   .offset = offset_get(&s->offset),
                                   .clock_delta = s->clock_delta,
                                   .open = s->info,
                                   .state = s->state};
    if (s->open && !s->ret) /* a DECODE session's pictures are the guest's, not a viewer's */
        m.flags |= HM_OPEN;
    if (s->have_state)
        m.flags |= HM_PLAY_STATE;
    if (s->have_delta && m.offset != NONE)
        m.flags |= HM_CLOCK;
    if (s->eos)
        m.flags |= HM_EOS;
    return m;
}

static int lease_slot(struct stream *s)
{
    for (unsigned i = 0; i < HOST_MEDIA_LEASES; i++)
        if (!s->leases[i].token)
            return (int)i;
    return -1;
}

/* EGL import does not promise reservation-fence publication. Peer EOF and
 * DMA-BUF POLLOUT therefore cannot authorize codec surface reuse. The first
 * unclean disconnect quarantines at most HOST_MEDIA_LEASES real AVFrame refs
 * until helper exit, and permanently switches this port to genuine downloads.
 * Subsequent connections receive immutable memfds and cannot grow quarantine.
 * This deliberately spends a fixed surface budget for unqualified crash safety;
 * extra_hw_frames includes that budget. No hardware-success claim is implied. */
int export_frame(struct stream *s, AVFrame *f)
{
    int slot = lease_slot(s);
    if (slot < 0 || !s->ready)
        return 0;
    if (f->format == AV_PIX_FMT_DRM_PRIME && (s->force_download || s->hw_import_failed)) {
        AVFrame *sw = av_frame_alloc();
        if (!sw)
            return 0;
        if (av_hwframe_transfer_data(sw, f, 0) < 0) {
            av_frame_free(&sw);
            return 0;
        }
        av_frame_copy_props(sw, f);
        if (sw->format == AV_PIX_FMT_YUV420P10LE || sw->format == AV_PIX_FMT_P010LE ||
            sw->format == AV_PIX_FMT_P016LE) {
            AVFrame *eight = sw->format == AV_PIX_FMT_YUV420P10LE ? to_8bit(sw) : p010_to_nv12(sw);
            av_frame_free(&sw);
            sw = eight;
        }
        if (!sw)
            return 0;
        int result = export_frame(s, sw);
        av_frame_free(&sw);
        return result;
    }
    struct host_media_message m = snapshot(s, HM_VIDEO);
    m.token = ++s->next_token;
    m.width = f->width;
    m.height = f->height;
    m.pts = f->pts;
    m.colorspace = f->colorspace;
    m.color_range = f->color_range;
    if (f->format == AV_PIX_FMT_YUVJ420P || f->format == AV_PIX_FMT_YUVJ422P ||
        f->format == AV_PIX_FMT_YUVJ444P)
        m.color_range = AVCOL_RANGE_JPEG;
    int fds[HOST_MEDIA_OBJECTS] = {-1, -1, -1, -1};
    int local_fd = -1;
    if (f->format == AV_PIX_FMT_DRM_PRIME) {
        const AVDRMFrameDescriptor *d = (const void *)f->data[0];
        if (!d || d->nb_objects < 1 || d->nb_objects > HOST_MEDIA_OBJECTS || d->nb_layers < 1 ||
            d->nb_layers > HOST_MEDIA_LAYERS)
            return 0;
        m.format = HM_DRM;
        m.objects = d->nb_objects;
        m.layers = d->nb_layers;
        for (int i = 0; i < d->nb_objects; i++) {
            fds[i] = d->objects[i].fd;
            m.object[i].size = d->objects[i].size;
            m.object[i].modifier = d->objects[i].format_modifier;
        }
        for (int l = 0; l < d->nb_layers; l++) {
            const AVDRMLayerDescriptor *layer = &d->layers[l];
            if (layer->nb_planes < 1 || layer->nb_planes > HOST_MEDIA_PLANES)
                return 0;
            m.layer[l].fourcc = layer->format;
            m.layer[l].planes = layer->nb_planes;
            for (int p = 0; p < layer->nb_planes; p++) {
                const AVDRMPlaneDescriptor *plane = &layer->planes[p];
                if (plane->offset < 0 || plane->pitch <= 0 || (uint64_t)plane->pitch > UINT32_MAX)
                    return 0;
                m.layer[l].plane[p] =
                    (struct host_media_plane){plane->object_index, plane->pitch, plane->offset};
            }
        }
    } else {
        switch (f->format) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            m.format = HM_YUV420;
            break;
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUVJ422P:
            m.format = HM_YUV422;
            break;
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_YUVJ444P:
            m.format = HM_YUV444;
            break;
        case AV_PIX_FMT_NV12:
            m.format = HM_NV12;
            break;
        default:
            return 0;
        }
        int bytes = av_image_get_buffer_size(f->format, f->width, f->height, 1);
        if (bytes <= 0 || bytes > (int)HOST_MEDIA_MAX_BYTES)
            return 0;
        struct hm_buffer buffer;
        if (hm_buffer_create(&buffer, bytes))
            return 0;
        uint8_t *packed = buffer.data, *planes[4];
        int strides[4];
        /* Copy native planes once, directly into their final immutable object.
         * No writable mapping survives hm_buffer_finish or reaches the viewer. */
        int copied = av_image_copy_to_buffer(packed, bytes, (const uint8_t *const *)f->data,
                                             f->linesize, f->format, f->width, f->height, 1);
        if (copied != bytes || av_image_fill_arrays(planes, strides, packed, f->format, f->width,
                                                    f->height, 1) != bytes) {
            hm_buffer_discard(&buffer);
            return 0;
        }
        m.objects = m.layers = 1;
        m.object[0].size = bytes;
        m.layer[0].planes = m.format == HM_NV12 ? 2 : 3;
        for (unsigned p = 0; p < m.layer[0].planes; p++)
            m.layer[0].plane[p] = (struct host_media_plane){0, strides[p], planes[p] - packed};
        local_fd = hm_buffer_finish(&buffer);
        if (local_fd < 0)
            return 0;
        fds[0] = local_fd;
    }
    if (!hm_validate_frame(&m, m.objects)) {
        if (local_fd >= 0)
            close(local_fd);
        return 0;
    }
    AVFrame *retained = m.format == HM_DRM ? av_frame_clone(f) : NULL;
    if (m.format == HM_DRM && !retained)
        return 0;
    int sent = hm_send(s->viewer, &m, fds, m.objects);
    if (local_fd >= 0)
        close(local_fd);
    if (sent == 1) {
        s->leases[slot].token = m.token;
        s->leases[slot].generation = m.generation;
        s->leases[slot].frame = retained;
    } else
        av_frame_free(&retained);
    return sent;
}

int export_audio(struct stream *s, struct chunk *c)
{
    int slot = lease_slot(s);
    if (slot < 0 || !s->ready)
        return 0;
    struct host_media_message m = snapshot(s, HM_PCM);
    m.token = ++s->next_token;
    m.pts = c->rt;
    m.format = HM_FLOAT_STEREO;
    m.samples = c->frames;
    m.objects = 1;
    m.object[0].size = (uint64_t)c->frames * 8;
    int fd = hm_memfd(c->data, m.object[0].size);
    if (fd < 0)
        return 0;
    int sent = hm_send(s->viewer, &m, &fd, 1);
    close(fd);
    if (sent == 1) {
        s->leases[slot].token = m.token;
        s->leases[slot].generation = m.generation;
    }
    return sent;
}
