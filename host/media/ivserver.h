// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef HOST_MEDIA_IVSERVER_H
#define HOST_MEDIA_IVSERVER_H
/* The media pool's ivshmem server (QEMU docs/specs/ivshmem-spec.rst, the client-server protocol):
 * the media helper serves QEMU's ivshmem-doorbell itself. The pool file is the shared memory;
 * the helper is peer 0 with one eventfd (the guest's doorbell rings it), and QEMU, the one client
 * it takes, gets ids that only count up: QEMU 10 frees a departed peer's eventfds and a later
 * message for the same id writes into the freed memory, so an id is never given twice. A second
 * client while QEMU is connected is refused (its id is spent too). */
#include <pthread.h>
#include <stdint.h>

struct ivserver {
    int listen_fd, shm_fd;
    int self_ev; /* peer 0's vector 0: the guest rings the host */
    int64_t next_id;
    int sock;             /* the client's connection, -1 without one */
    int64_t peer;         /* its id */
    int guest_ev;         /* its vector 0: the host rings the guest */
    pthread_mutex_t lock; /* guest_ev against ivserver_ring_guest from other threads */
    unsigned long long accepted, refused, left;
};

/* listen_fd: a listening unix socket (the launcher's); shm_fd: the pool file. 0 or -1. */
int ivserver_init(struct ivserver *s, int listen_fd, int shm_fd);
void ivserver_fini(struct ivserver *s);
/* A connection is waiting on listen_fd: the new client's id, or -1 (refused or failed). */
int64_t ivserver_accept(struct ivserver *s);
/* The client's socket is readable or hung up: 1 when it left (its eventfd closed), else 0. */
int ivserver_client_event(struct ivserver *s);
/* Rings the guest (vector 0 of the client): 0, or -1 without a client. Any thread. */
int ivserver_ring_guest(struct ivserver *s);
/* Takes the host's doorbell (self_ev readable): the count since the last call. */
uint64_t ivserver_take_doorbell(struct ivserver *s);
#endif
