// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The media pool's ivshmem server (host/media/ivserver.c) as QEMU's ivshmem-doorbell sees it: the
 * setup messages, ids that only count up (a second client refused while one is connected, a
 * departed client's id never given again), the doorbell both ways. Prints TAP; exit status 0
 * only if every check passed. */
#define _GNU_SOURCE
#include "../../../host/media/ivserver.h"

#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int n, failed;
static void ok(int cond, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("%s %d - ", cond ? "ok" : "not ok", ++n);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    failed += !cond;
}

static struct sockaddr_un addr;

/* One protocol message: its value, and its fd (-1 without). 0, or -1 at the end. */
static int recv_msg(int s, int64_t *v, int *fd)
{
    char cb[CMSG_SPACE(sizeof(int))];
    struct iovec io = {v, sizeof *v};
    struct msghdr m = {
        .msg_iov = &io, .msg_iovlen = 1, .msg_control = cb, .msg_controllen = sizeof cb};
    *fd = -1;
    if (recvmsg(s, &m, MSG_CMSG_CLOEXEC) != sizeof *v)
        return -1;
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    if (c && c->cmsg_type == SCM_RIGHTS)
        memcpy(fd, CMSG_DATA(c), sizeof(int));
    return 0;
}

struct client {
    int sock, shm, host_ev, own_ev;
    int64_t id, host_id;
};

/* Connects as QEMU does and reads the setup: 0, or -1 when refused. */
static int connect_client(struct ivserver *s, struct client *c)
{
    memset(c, 0, sizeof *c);
    c->sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (connect(c->sock, (struct sockaddr *)&addr, sizeof addr))
        return -1;
    ivserver_accept(s);
    int64_t v;
    int fd;
    if (recv_msg(c->sock, &v, &fd) || v != 0 || fd >= 0) /* version */
        return -1;
    if (recv_msg(c->sock, &c->id, &fd) || fd >= 0)
        return -1;
    if (recv_msg(c->sock, &v, &c->shm) || v != -1 || c->shm < 0)
        return -1;
    if (recv_msg(c->sock, &c->host_id, &c->host_ev) || c->host_ev < 0)
        return -1;
    if (recv_msg(c->sock, &v, &c->own_ev) || v != c->id || c->own_ev < 0)
        return -1;
    return 0;
}

static void close_client(struct client *c)
{
    close(c->sock);
    if (c->shm > 0)
        close(c->shm);
    if (c->host_ev > 0)
        close(c->host_ev);
    if (c->own_ev > 0)
        close(c->own_ev);
}

static int readable(int fd)
{
    struct pollfd p = {fd, POLLIN, 0};
    return poll(&p, 1, 200) == 1;
}

int main(void)
{
    char dir[] = "/tmp/check-ivserver-XXXXXX";
    if (!mkdtemp(dir))
        return 2;
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s/pool.sock", dir);
    int l = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (bind(l, (struct sockaddr *)&addr, sizeof addr) || listen(l, 4))
        return 2;
    int shm = memfd_create("check-ivserver", MFD_CLOEXEC);
    if (shm < 0 || ftruncate(shm, 1 << 20))
        return 2;
    struct ivserver s;
    ok(ivserver_init(&s, l, shm) == 0, "server up");
    ok(ivserver_ring_guest(&s) == -1, "no guest to ring before QEMU connects");

    struct client a, b, c;
    ok(connect_client(&s, &a) == 0, "QEMU's setup: version, id, memory, the host's and its own "
                                    "eventfd");
    ok(a.id == 1 && a.host_id == 0, "host is peer 0, QEMU peer %lld", (long long)a.id);
    ok(ivserver_ring_guest(&s) == 0 && readable(a.own_ev), "the host rings the guest's vector");
    uint64_t one = 1;
    ok(write(a.host_ev, &one, sizeof one) == sizeof one && ivserver_take_doorbell(&s) == 1,
       "the guest's doorbell reaches the host");
    ok(ivserver_take_doorbell(&s) == 0, "taken once");

    /* a second client while QEMU is there: refused, its id spent */
    b.sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    connect(b.sock, (struct sockaddr *)&addr, sizeof addr);
    ok(ivserver_accept(&s) == -1 && s.refused == 1, "a second client refused");
    char x;
    ok(recv(b.sock, &x, 1, 0) == 0, "its connection closed");
    close(b.sock);

    /* QEMU leaves: its eventfd goes, ringing fails, the next client gets a new id */
    close_client(&a);
    ok(ivserver_client_event(&s) == 1 && s.left == 1, "QEMU's departure seen");
    ok(ivserver_ring_guest(&s) == -1, "no guest to ring after it left");
    ok(connect_client(&s, &c) == 0 && c.id == 3, "the next client is peer %lld: ids never reused",
       (long long)c.id);
    ok(ivserver_client_event(&s) == 0, "a client that only sits there stays");
    close_client(&c);
    ivserver_fini(&s);
    close(l);
    close(shm);
    unlink(addr.sun_path);
    rmdir(dir);
    printf("1..%d\n", n);
    return failed ? 1 : 0;
}
