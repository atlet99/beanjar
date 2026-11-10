// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* A stream relay: each LISTEN=CONNECT pair accepts connections on LISTEN and joins each one to a
 * new connection to CONNECT, both ways, until both sides are done. mode=mitm's port bridges
 * (host/core/beanjar/netns.py): one relay on the host for the inspector and the forwarded ports
 * (tcp -> the namespace's unix sockets), one in link=lan's LAN namespace, one inside the inner
 * namespace for the other end of each (unix -> tcp).
 *
 *   beanjar-relay LISTEN=CONNECT...      each side tcp:HOST:PORT or unix:PATH
 *
 * A tcp listener binds with SO_REUSEADDR; a unix listener's path is unlinked first if it is a
 * socket. EOF on one side shuts down writing on the other (a half-close) and the pair ends when
 * both directions have; a reset or error on either side ends it. A failed connect to CONNECT closes
 * the accepted connection. Once every listener is bound it prints "ready" on stdout (and nothing
 * more there); a listener that cannot bind ends it with status 1 and the reason on stderr, before
 * "ready". SIGTERM or SIGINT closes the listeners and every connection, removes its unix sockets
 * and ends it with status 0. Single-threaded, epoll; it replaced a Python asyncio relay of about
 * 10 MB PSS per process. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define BUF        (64 * 1024)
#define MAX_LISTEN 64

enum { LISTENER, SIDE, SIGNALS };

struct addr {
    int unix_;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path) + 64]; /* too long: refused at use */
    char host[256], port[8];
    char text[512];
};

struct listener {
    int kind; /* LISTENER */
    int fd;
    const struct addr *listen, *connect;
};

struct conn;
struct side {
    int kind;      /* SIDE */
    int fd, index; /* 0: accepted, 1: ours to CONNECT */
    int watched;   /* in the epoll set */
    struct conn *c;
};

/* dir[i]: bytes read from side i, still to be written to side 1 - i */
struct dir {
    char *buf;
    size_t len, off;
    int eof, shut;
};

struct conn {
    struct side s[2];
    struct dir dir[2];
    int connecting, dead;
    const struct addr *target;
    struct conn *next, *prev;
};

static int ep;
static struct conn *conns,
    *graveyard; /* graveyard: ended in this batch of events, freed after it */
static struct listener listeners[MAX_LISTEN];
static int nlisteners;
static struct {
    char path[sizeof(((struct addr *)0)->path)];
    dev_t dev;
    ino_t ino;
} made[MAX_LISTEN];
static int nmade;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("relay: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int parse_addr(const char *s, struct addr *a)
{
    memset(a, 0, sizeof *a);
    snprintf(a->text, sizeof a->text, "%s", s);
    if (!strncmp(s, "unix:", 5) && s[5]) {
        a->unix_ = 1;
        if (strlen(s + 5) >= sizeof a->path)
            return -1;
        strcpy(a->path, s + 5);
        return 0;
    }
    if (strncmp(s, "tcp:", 4))
        return -1;
    const char *rest = s + 4, *colon = strrchr(rest, ':');
    if (!colon || colon == rest || !colon[1] ||
        strspn(colon + 1, "0123456789") != strlen(colon + 1) || strlen(colon + 1) >= sizeof a->port)
        return -1;
    size_t n = (size_t)(colon - rest);
    if (n >= 2 && rest[0] == '[' && rest[n - 1] == ']') {
        rest++;
        n -= 2;
    } /* [::1] */
    if (!n || n >= sizeof a->host)
        return -1;
    memcpy(a->host, rest, n);
    strcpy(a->port, colon + 1);
    snprintf(a->text, sizeof a->text, "tcp:%s:%s", a->host, a->port);
    return 0;
}

static void unlink_made(void)
{
    for (int i = 0; i < nmade; i++) {
        struct stat st;
        if (!lstat(made[i].path, &st) && st.st_dev == made[i].dev && st.st_ino == made[i].ino)
            unlink(made[i].path);
    }
    nmade = 0;
}

static int unix_sockaddr(const struct addr *a, struct sockaddr_un *sa)
{
    memset(sa, 0, sizeof *sa);
    sa->sun_family = AF_UNIX;
    if (strlen(a->path) >= sizeof sa->sun_path) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(sa->sun_path, a->path);
    return 0;
}

static void watch(int fd, void *ptr, uint32_t events, int op)
{
    struct epoll_event e = {.events = events, .data.ptr = ptr};
    epoll_ctl(ep, op, fd, &e);
}

static int add_listener(int fd, const struct addr *l, const struct addr *c)
{
    if (nlisteners == MAX_LISTEN) {
        close(fd);
        errno = EMFILE;
        return -1;
    }
    struct listener *x = &listeners[nlisteners++];
    *x = (struct listener){.kind = LISTENER, .fd = fd, .listen = l, .connect = c};
    watch(fd, x, EPOLLIN, EPOLL_CTL_ADD);
    return 0;
}

/* Bind LISTEN; 0, or -1 with the reason in *why. */
static int listen_on(const struct addr *l, const struct addr *c, const char **why)
{
    if (l->unix_) {
        struct sockaddr_un sa;
        struct stat st;
        if (unix_sockaddr(l, &sa) < 0) {
            *why = strerror(errno);
            return -1;
        }
        if (!lstat(l->path, &st) && S_ISSOCK(st.st_mode))
            unlink(l->path);
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0 || bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
            *why = strerror(errno);
            if (fd >= 0)
                close(fd);
            return -1;
        }
        if (!stat(l->path, &st) && nmade < MAX_LISTEN) {
            snprintf(made[nmade].path, sizeof made[nmade].path, "%s", l->path);
            made[nmade].dev = st.st_dev;
            made[nmade].ino = st.st_ino;
            nmade++;
        }
        if (listen(fd, SOMAXCONN) < 0 || add_listener(fd, l, c) < 0) {
            *why = strerror(errno);
            return -1;
        }
        return 0;
    }
    struct addrinfo hints = {.ai_family = AF_UNSPEC,
                             .ai_socktype = SOCK_STREAM,
                             .ai_flags = AI_PASSIVE},
                    *res;
    int r = getaddrinfo(l->host, l->port, &hints, &res);
    if (r) {
        *why = gai_strerror(r);
        return -1;
    }
    int n = 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                        ai->ai_protocol),
            one = 1;
        if (fd < 0) {
            *why = strerror(errno);
            n = -1;
            break;
        }
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (ai->ai_family == AF_INET6)
            setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) < 0 || listen(fd, SOMAXCONN) < 0) {
            *why = strerror(errno);
            close(fd);
            n = -1;
            break;
        }
        if (add_listener(fd, l, c) < 0) {
            *why = strerror(errno);
            n = -1;
            break;
        }
        n++;
    }
    freeaddrinfo(res);
    return n > 0 ? 0 : -1;
}

static void conn_close(struct conn *c)
{
    for (int i = 0; i < 2; i++) {
        if (c->s[i].fd >= 0)
            close(c->s[i].fd); /* also leaves the epoll set */
        c->s[i].fd = -1;
        free(c->dir[i].buf);
        c->dir[i].buf = NULL;
    }
    if (c->prev)
        c->prev->next = c->next;
    else
        conns = c->next;
    if (c->next)
        c->next->prev = c->prev;
    c->dead = 1;
    c->next = graveyard;
    graveyard = c;
}

static void bury(void)
{
    while (graveyard) {
        struct conn *c = graveyard;
        graveyard = c->next;
        free(c);
    }
}

/* Interest of both sides from the buffers' state. A side with nothing to wait for leaves the epoll
 * set: epoll reports a hang-up whatever the interest, and a side that hung up while what it sent
 * still waits in its buffer (a client that sent, half-closed and was answered) would otherwise
 * report it again and again until the other side has taken that data. */
static void rearm(struct conn *c)
{
    for (int i = 0; i < 2; i++) {
        struct side *s = &c->s[i];
        uint32_t ev = 0;
        if (s->fd < 0)
            continue;
        if (c->connecting)
            ev = i == 1 ? EPOLLOUT : 0; /* nothing is read before it is connected */
        else {
            if (!c->dir[i].eof && !c->dir[i].len)
                ev |= EPOLLIN;
            if (c->dir[1 - i].len)
                ev |= EPOLLOUT;
        }
        if (ev) {
            watch(s->fd, s, ev, s->watched ? EPOLL_CTL_MOD : EPOLL_CTL_ADD);
            s->watched = 1;
        } else if (s->watched) {
            epoll_ctl(ep, EPOLL_CTL_DEL, s->fd, NULL);
            s->watched = 0;
        }
    }
}

/* Write what dir[i] holds to side 1 - i, then pass its EOF on; -1 if the pair must end. */
static int flush(struct conn *c, int i)
{
    struct dir *d = &c->dir[i];
    int to = c->s[1 - i].fd;
    while (d->len) {
        ssize_t n = send(to, d->buf + d->off, d->len - d->off, MSG_NOSIGNAL);
        if (n < 0)
            return errno == EAGAIN || errno == EINTR ? 0 : -1;
        d->off += (size_t)n;
        if (d->off == d->len)
            d->len = d->off = 0;
    }
    if (d->eof && !d->shut) {
        shutdown(to, SHUT_WR);
        d->shut = 1;
    }
    return 0;
}

static int pull(struct conn *c, int i)
{
    struct dir *d = &c->dir[i];
    if (d->eof || d->len)
        return 0;
    ssize_t n = recv(c->s[i].fd, d->buf, BUF, 0);
    if (n < 0)
        return errno == EAGAIN || errno == EINTR ? 0 : -1;
    if (n == 0)
        d->eof = 1;
    else {
        d->len = (size_t)n;
        d->off = 0;
    }
    return flush(c, i);
}

static void nodelay(int fd)
{
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one); /* fails harmlessly on unix */
}

/* Start the connection to the pair's CONNECT for an accepted fd. */
static void start(int fd, const struct addr *target)
{
    struct conn *c = calloc(1, sizeof *c);
    if (c) {
        c->dir[0].buf = malloc(BUF);
        c->dir[1].buf = malloc(BUF);
    }
    if (!c || !c->dir[0].buf || !c->dir[1].buf) {
        say("out of memory");
        if (c) {
            free(c->dir[0].buf);
            free(c->dir[1].buf);
            free(c);
        }
        close(fd);
        return;
    }
    c->target = target;
    c->s[0] = (struct side){.kind = SIDE, .fd = fd, .index = 0, .c = c};
    c->s[1] = (struct side){.kind = SIDE, .fd = -1, .index = 1, .c = c};
    c->next = conns;
    if (conns)
        conns->prev = c;
    conns = c;
    nodelay(fd);
    int out = -1, r = -1;
    if (target->unix_) {
        struct sockaddr_un sa;
        if (unix_sockaddr(target, &sa) == 0 &&
            (out = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) >= 0)
            r = connect(out, (struct sockaddr *)&sa, sizeof sa);
    } else {
        struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM}, *res;
        int g = getaddrinfo(target->host, target->port, &hints, &res);
        if (g) {
            say("connect %s: %s", target->text, gai_strerror(g));
            conn_close(c);
            return;
        }
        if ((out = socket(res->ai_family, res->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          res->ai_protocol)) >= 0) {
            nodelay(out);
            r = connect(out, res->ai_addr, res->ai_addrlen);
        }
        freeaddrinfo(res);
    }
    if (out < 0 || (r < 0 && errno != EINPROGRESS)) {
        say("connect %s: %s", target->text, strerror(errno));
        if (out >= 0)
            close(out);
        conn_close(c);
        return;
    }
    c->s[1].fd = out;
    c->connecting = r < 0;
    rearm(c);
}

static void side_event(struct side *s, uint32_t ev)
{
    struct conn *c = s->c;
    int i = s->index;
    if (i == 1 && c->connecting) {
        int err = 0;
        socklen_t len = sizeof err;
        if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
            err = errno;
        if (err) {
            say("connect %s: %s", c->target->text, strerror(err));
            conn_close(c);
            return;
        }
        if (!(ev & (EPOLLOUT | EPOLLERR | EPOLLHUP)))
            return;
        c->connecting = 0;
        rearm(c);
        return;
    }
    if (ev & EPOLLERR) {
        conn_close(c);
        return;
    }
    /* writable (or hung up, which the write then reports): what the other side sent this one */
    if ((ev & (EPOLLOUT | EPOLLHUP)) && c->dir[1 - i].len && flush(c, 1 - i) < 0) {
        conn_close(c);
        return;
    }
    /* readable, or hung up: the rest of what it sent, then its EOF */
    if ((ev & (EPOLLIN | EPOLLHUP)) && pull(c, i) < 0) {
        conn_close(c);
        return;
    }
    if (c->dir[0].shut && c->dir[1].shut) {
        conn_close(c);
        return;
    }
    rearm(c);
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fprintf(stderr, "usage: beanjar-relay LISTEN=CONNECT...  (tcp:HOST:PORT or unix:PATH)\n");
        return 2;
    }
    int npairs = argc - 1;
    struct addr *addrs = calloc((size_t)npairs * 2, sizeof *addrs);
    if (!addrs)
        return 1;
    for (int i = 0; i < npairs; i++) {
        char *arg = strdup(argv[i + 1]), *eq = arg ? strchr(arg, '=') : NULL;
        if (!eq) {
            say("bad pair '%s' (LISTEN=CONNECT)", argv[i + 1]);
            return 1;
        }
        *eq = 0;
        const char *bad = parse_addr(arg, &addrs[2 * i])          ? arg
                          : parse_addr(eq + 1, &addrs[2 * i + 1]) ? eq + 1
                                                                  : NULL;
        if (bad) {
            say("bad address '%s' (tcp:HOST:PORT or unix:PATH)", bad);
            return 1;
        }
        free(arg);
    }
    signal(SIGPIPE, SIG_IGN);
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    ep = epoll_create1(EPOLL_CLOEXEC);
    if (sfd < 0 || ep < 0) {
        say("%s", strerror(errno));
        return 1;
    }
    static int signals_kind = SIGNALS;
    watch(sfd, &signals_kind, EPOLLIN, EPOLL_CTL_ADD);
    for (int i = 0; i < npairs; i++) {
        const char *why = "";
        if (listen_on(&addrs[2 * i], &addrs[2 * i + 1], &why) < 0) {
            say("cannot listen on %s: %s", addrs[2 * i].text, why);
            unlink_made();
            return 1;
        }
        say("%s -> %s", addrs[2 * i].text, addrs[2 * i + 1].text);
    }
    fputs("ready\n", stdout);
    fflush(stdout);
    int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (null >= 0) {
        dup2(null, 1);
        close(null);
    }
    for (;;) {
        struct epoll_event evs[64];
        int n = epoll_wait(ep, evs, 64, -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            say("epoll: %s", strerror(errno));
            break;
        }
        for (int k = 0; k < n; k++) {
            int kind = *(int *)evs[k].data.ptr;
            if (kind == SIGNALS)
                goto done;
            if (kind == LISTENER) {
                struct listener *l = evs[k].data.ptr;
                for (;;) {
                    int fd = accept4(l->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (fd < 0) {
                        if (errno != EAGAIN && errno != EINTR && errno != ECONNABORTED)
                            say("accept %s: %s", l->listen->text, strerror(errno));
                        break;
                    }
                    start(fd, l->connect);
                }
                continue;
            }
            /* a side: its connection may have ended earlier in this batch */
            struct side *s = evs[k].data.ptr;
            if (!s->c->dead)
                side_event(s, evs[k].events);
        }
        bury();
    }
done:
    for (int i = 0; i < nlisteners; i++)
        close(listeners[i].fd);
    unlink_made();
    while (conns)
        conn_close(conns);
    bury();
    return 0;
}
