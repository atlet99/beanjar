// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef HOST_MEDIA_IPC_H
#define HOST_MEDIA_IPC_H
#include <stddef.h>
#include <stdint.h>
#include "../../include/host-media.h"
/* Returns 1 for one message, 0 for EAGAIN, -1 for closure/malformed/error.
 * On receive failure every received descriptor is closed. */
int hm_send(int fd, const struct host_media_message *m, const int *fds, unsigned nfds);
int hm_receive(int fd, struct host_media_message *m, int fds[HOST_MEDIA_OBJECTS], unsigned *nfds);
void hm_close_fds(int *fds, unsigned n);
int hm_validate_frame(const struct host_media_message *m, unsigned nfds);
/* Private construction state. Only a successful finish returns a transferable
 * FD: it unmaps writable storage before applying every required seal. Finish
 * consumes the builder on both success and failure; discard aborts a copy. */
struct hm_buffer {
    int fd;
    void *data;
    size_t size;
};
int hm_buffer_create(struct hm_buffer *buffer, size_t bytes);
int hm_buffer_finish(struct hm_buffer *buffer);
void hm_buffer_discard(struct hm_buffer *buffer);
int hm_memfd(const void *data, size_t bytes);
int64_t hm_mono_ns(void);
/* A PICTURE message for the guest (include/webos-media.h): header and *p, then bytes the caller
 * fills at the returned pointer + *len - bytes; sequence is the sender's. NULL without memory. */
/* Any message for the guest: header (type, sequence) and size bytes of payload. */
unsigned char *hm_guest_message(uint16_t type, uint32_t sequence, const void *payload, size_t size,
                                size_t *len);
unsigned char *hm_picture(uint32_t sequence, const struct webos_media_picture *p, size_t bytes,
                          size_t *len);
#endif
