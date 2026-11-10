// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The media pool's ivshmem server (ivserver.h). */
#define _GNU_SOURCE
#include "ivserver.h"

#include <errno.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#define SELF_ID 0

static int send_msg(int sock, int64_t value, int fd)
{
    char cb[CMSG_SPACE(sizeof(int))];
    struct iovec io = {&value, sizeof value};
    struct msghdr m = {.msg_iov = &io, .msg_iovlen = 1};
    if (fd >= 0) {
        memset(cb, 0, sizeof cb);
        m.msg_control = cb;
        m.msg_controllen = sizeof cb;
        struct cmsghdr *c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &fd, sizeof(int));
    }
    return sendmsg(sock, &m, MSG_NOSIGNAL) == (ssize_t)sizeof value ? 0 : -1;
}

int ivserver_init(struct ivserver *s, int listen_fd, int shm_fd)
{
    memset(s, 0, sizeof *s);
    s->listen_fd = listen_fd;
    s->shm_fd = shm_fd;
    s->sock = s->guest_ev = -1;
    s->peer = -1;
    s->next_id = SELF_ID + 1;
    pthread_mutex_init(&s->lock, NULL);
    s->self_ev = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    return s->self_ev < 0 ? -1 : 0;
}

static void drop_client(struct ivserver *s)
{
    pthread_mutex_lock(&s->lock);
    if (s->guest_ev >= 0)
        close(s->guest_ev);
    s->guest_ev = -1;
    pthread_mutex_unlock(&s->lock);
    if (s->sock >= 0)
        close(s->sock);
    s->sock = -1;
    s->peer = -1;
}

void ivserver_fini(struct ivserver *s)
{
    drop_client(s);
    if (s->self_ev >= 0)
        close(s->self_ev);
    s->self_ev = -1;
}

int64_t ivserver_accept(struct ivserver *s)
{
    int sock = accept4(s->listen_fd, NULL, NULL, SOCK_CLOEXEC);
    if (sock < 0)
        return -1;
    int64_t id = s->next_id++; /* spent even when refused: never given twice */
    if (s->sock >= 0) {
        s->refused++;
        close(sock);
        return -1;
    }
    int ev = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    /* version, its id, the memory, the host's vector, then its own vector */
    if (ev < 0 || send_msg(sock, 0, -1) || send_msg(sock, id, -1) ||
        send_msg(sock, -1, s->shm_fd) || send_msg(sock, SELF_ID, s->self_ev) ||
        send_msg(sock, id, ev)) {
        if (ev >= 0)
            close(ev);
        close(sock);
        s->refused++;
        return -1;
    }
    s->sock = sock;
    s->peer = id;
    pthread_mutex_lock(&s->lock);
    s->guest_ev = ev;
    pthread_mutex_unlock(&s->lock);
    s->accepted++;
    return id;
}

int ivserver_client_event(struct ivserver *s)
{
    if (s->sock < 0)
        return 0;
    char b[64];
    ssize_t r = recv(s->sock, b, sizeof b, MSG_DONTWAIT);
    if (r > 0 || (r < 0 && (errno == EAGAIN || errno == EINTR)))
        return 0; /* clients send nothing; ignored */
    drop_client(s);
    s->left++;
    return 1;
}

int ivserver_ring_guest(struct ivserver *s)
{
    uint64_t one = 1;
    int r = -1;
    pthread_mutex_lock(&s->lock);
    if (s->guest_ev >= 0 && write(s->guest_ev, &one, sizeof one) == (ssize_t)sizeof one)
        r = 0;
    pthread_mutex_unlock(&s->lock);
    return r;
}

uint64_t ivserver_take_doorbell(struct ivserver *s)
{
    uint64_t n = 0;
    if (read(s->self_ev, &n, sizeof n) != (ssize_t)sizeof n)
        return 0;
    return n;
}
