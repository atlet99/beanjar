// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
/* beanjar-vnc's audio socket (audio.h): the request and the header on the main loop, the stream
 * from the core's audio clock thread (the wv_audio_sink callback), which never waits on a
 * client. */
#include "audio.h"
#include "../viewer/core/core.h"
#include <errno.h>
#include <glib.h>
#include <glib-unix.h>
#include <opus.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

enum { FORMAT_PCM = 1, FORMAT_OPUS = 2 };
#define RATE         48000
#define OPUS_FRAMES  480 /* 10 ms: one core mix block */
#define OPUS_BITRATE 128000
#define OPUS_MAX     1500    /* bytes a packet may take */
#define PENDING      2048    /* the rest of one block or record a short write left */
#define SNDBUF       49152   /* with the kernel's overhead, about 200 ms of PCM queued */
#define SLOW_US      2000000 /* nothing taken for this long: disconnected */
#define REQUEST_MS   5000

struct client {
    struct client *next;
    struct audio_server *s;
    int fd, format;
    char req[32];
    unsigned reqlen;
    guint watch, timeout;
    unsigned char pend[PENDING];
    unsigned plen, poff;
    gint64 since; /* the first block dropped in a row, monotonic us; 0: none */
    int dead;
};

struct audio_server {
    struct wv *v;
    char *path;
    int fd;
    guint watch;
    struct client *requesting; /* main thread */
    pthread_mutex_t lock;
    struct client *clients; /* streaming (lock) */
    int npcm, nopus;
    OpusEncoder *enc;
    short acc[2 * OPUS_FRAMES];
    int accn, opus_was;
    unsigned long long served, refused, dropped, slow, packets, opus_bytes, pcm_blocks;
};

static void client_close(struct client *c)
{
    if (c->watch)
        g_source_remove(c->watch);
    if (c->timeout)
        g_source_remove(c->timeout);
    close(c->fd);
    free(c);
}

static void unlink_requesting(struct client *c)
{
    for (struct client **p = &c->s->requesting; *p; p = &(*p)->next)
        if (*p == c) {
            *p = c->next;
            return;
        }
}

static void wanted(struct audio_server *s)
{
    wv_audio_wanted(s->v, s->npcm + s->nopus > 0);
}

/* One block or record to c, whole or not at all (a short write keeps its rest for the next). */
static void send_block(struct client *c, const void *buf, size_t n)
{
    gint64 now = g_get_monotonic_time();
    if (c->plen) {
        ssize_t r = send(c->fd, c->pend + c->poff, c->plen - c->poff, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (r < 0 && errno != EAGAIN && errno != EINTR) {
            c->dead = 1;
            return;
        }
        if (r > 0)
            c->poff += (unsigned)r;
        if (c->poff == c->plen)
            c->plen = c->poff = 0;
    }
    ssize_t r = c->plen ? -1 : send(c->fd, buf, n, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r < 0 && !c->plen && errno != EAGAIN && errno != EINTR) {
        c->dead = 1;
        return;
    }
    if (r < 0) {
        c->s->dropped++;
        if (!c->since)
            c->since = now;
        else if (now - c->since > SLOW_US) {
            c->s->slow++;
            c->dead = 1;
        }
        return;
    }
    c->since = 0;
    if ((size_t)r < n) {
        memcpy(c->pend, (const unsigned char *)buf + r, n - (size_t)r);
        c->plen = (unsigned)(n - (size_t)r);
        c->poff = 0;
    }
}

static void each(struct audio_server *s, int format, const void *buf, size_t n)
{
    for (struct client *c = s->clients; c; c = c->next)
        if (c->format == format && !c->dead)
            send_block(c, buf, n);
}

/* wv_audio_sink: 10 ms of the mix, on the core's audio clock thread. */
static void sink(void *data, const short *pcm, int frames)
{
    struct audio_server *s = data;
    pthread_mutex_lock(&s->lock);
    if (s->npcm) {
        each(s, FORMAT_PCM, pcm, (size_t)frames * 4);
        s->pcm_blocks++;
    }
    if (s->nopus && s->enc) {
        if (!s->opus_was) {
            /* a first Opus listener: a fresh encoder state, the packet starts with this block */
            opus_encoder_ctl(s->enc, OPUS_RESET_STATE);
            s->accn = 0;
        }
        for (int i = 0; i < frames; i++) {
            s->acc[2 * s->accn] = pcm[2 * i];
            s->acc[2 * s->accn + 1] = pcm[2 * i + 1];
            if (++s->accn == OPUS_FRAMES) {
                unsigned char rec[2 + OPUS_MAX];
                opus_int32 len = opus_encode(s->enc, s->acc, OPUS_FRAMES, rec + 2, OPUS_MAX);
                s->accn = 0;
                if (len < 0) {
                    fprintf(stderr, "beanjar-vnc: opus_encode: %s\n", opus_strerror(len));
                    continue;
                }
                rec[0] = (unsigned char)len;
                rec[1] = (unsigned char)(len >> 8);
                each(s, FORMAT_OPUS, rec, (size_t)len + 2);
                s->packets++;
                s->opus_bytes += (unsigned long long)len;
            }
        }
    }
    s->opus_was = s->nopus > 0;
    int changed = 0;
    for (struct client **p = &s->clients; *p;) {
        struct client *c = *p;
        if (!c->dead) {
            p = &c->next;
            continue;
        }
        *p = c->next;
        *(c->format == FORMAT_OPUS ? &s->nopus : &s->npcm) -= 1;
        close(c->fd);
        free(c);
        changed = 1;
    }
    if (changed)
        wanted(s);
    pthread_mutex_unlock(&s->lock);
}

static void refuse(struct client *c, const char *why)
{
    fprintf(stderr, "beanjar-vnc: audio client refused: %s\n", why);
    c->s->refused++;
    unlink_requesting(c);
    client_close(c);
}

static gboolean request_timeout(gpointer data)
{
    struct client *c = data;
    c->timeout = 0;
    refuse(c, "no request line");
    return G_SOURCE_REMOVE;
}

/* The request line: "opus" or "pcm"; then the header, and the client streams. */
static gboolean request(gint fd, GIOCondition cond, gpointer data)
{
    (void)cond;
    struct client *c = data;
    ssize_t r = recv(fd, c->req + c->reqlen, sizeof c->req - 1 - c->reqlen, MSG_DONTWAIT);
    if (r < 0 && (errno == EAGAIN || errno == EINTR))
        return G_SOURCE_CONTINUE;
    if (r > 0) {
        c->reqlen += (unsigned)r;
        c->req[c->reqlen] = 0;
    }
    char *end = r > 0 ? strchr(c->req, '\n') : NULL;
    if (r > 0 && !end && c->reqlen < sizeof c->req - 1)
        return G_SOURCE_CONTINUE; /* the rest of the line to come */
    c->watch = 0;                 /* this source ends with this call */
    if (!end) {
        refuse(c, r > 0 ? "a request longer than a line" : "closed before its request");
        return G_SOURCE_REMOVE;
    }
    *end = 0;
    g_strstrip(c->req);
    int format = !strcmp(c->req, "pcm") ? FORMAT_PCM : !strcmp(c->req, "opus") ? FORMAT_OPUS : 0;
    if (!format || (format == FORMAT_OPUS && !c->s->enc)) {
        refuse(c, "not 'opus' or 'pcm'");
        return G_SOURCE_REMOVE;
    }
    unsigned char h[16] = {'B', 'J', 'A', 'U', 16, 0, 0, 0};
    h[8] = RATE & 0xff;
    h[9] = (RATE >> 8) & 0xff;
    h[10] = (RATE >> 16) & 0xff;
    h[12] = 2;
    h[14] = (unsigned char)format;
    if (send(fd, h, sizeof h, MSG_DONTWAIT | MSG_NOSIGNAL) != (ssize_t)sizeof h) {
        refuse(c, "the header could not be sent");
        return G_SOURCE_REMOVE;
    }
    if (c->timeout)
        g_source_remove(c->timeout);
    c->timeout = 0;
    struct audio_server *s = c->s;
    unlink_requesting(c);
    c->format = format;
    s->served++;
    pthread_mutex_lock(&s->lock);
    c->next = s->clients;
    s->clients = c;
    *(format == FORMAT_OPUS ? &s->nopus : &s->npcm) += 1;
    wanted(s);
    pthread_mutex_unlock(&s->lock);
    return G_SOURCE_REMOVE;
}

static gboolean accept_ready(gint fd, GIOCondition cond, gpointer data)
{
    (void)cond;
    struct audio_server *s = data;
    int cfd = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (cfd < 0)
        return G_SOURCE_CONTINUE;
    int size = SNDBUF;
    setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
    struct client *c = calloc(1, sizeof *c);
    c->s = s;
    c->fd = cfd;
    c->next = s->requesting;
    s->requesting = c;
    c->watch = g_unix_fd_add(cfd, G_IO_IN | G_IO_HUP | G_IO_ERR, request, c);
    c->timeout = g_timeout_add(REQUEST_MS, request_timeout, c);
    return G_SOURCE_CONTINUE;
}

struct audio_server *audio_server_new(const char *path, struct wv *v)
{
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof addr.sun_path) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(addr.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return NULL;
    unlink(path); /* a previous run's */
    /* owner-only from its bind on: no moment another user can connect */
    mode_t mask = umask(0177);
    int r = bind(fd, (struct sockaddr *)&addr, sizeof addr);
    umask(mask);
    if (r < 0 || listen(fd, 8) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return NULL;
    }
    struct audio_server *s = calloc(1, sizeof *s);
    s->v = v;
    s->path = strdup(path);
    s->fd = fd;
    pthread_mutex_init(&s->lock, NULL);
    int err;
    s->enc = opus_encoder_create(RATE, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (!s->enc)
        fprintf(stderr, "beanjar-vnc: no Opus encoder (%s): PCM only\n", opus_strerror(err));
    else
        opus_encoder_ctl(s->enc, OPUS_SET_BITRATE(OPUS_BITRATE));
    s->watch = g_unix_fd_add(fd, G_IO_IN, accept_ready, s);
    wv_audio_sink(v, sink, s);
    return s;
}

void audio_server_free(struct audio_server *s)
{
    if (!s)
        return;
    g_source_remove(s->watch);
    close(s->fd);
    unlink(s->path);
    while (s->requesting) {
        struct client *c = s->requesting;
        s->requesting = c->next;
        client_close(c);
    }
    pthread_mutex_lock(&s->lock);
    while (s->clients) {
        struct client *c = s->clients;
        s->clients = c->next;
        close(c->fd);
        free(c);
    }
    pthread_mutex_unlock(&s->lock);
    fprintf(stderr,
            "beanjar-vnc: audio clients=%llu refused=%llu pcm_blocks=%llu opus_packets=%llu "
            "(%.0f kbit/s) dropped=%llu slow_disconnects=%llu\n",
            s->served, s->refused, s->pcm_blocks, s->packets,
            s->packets ? s->opus_bytes * 8.0 / (s->packets * 0.02) / 1000 : 0, s->dropped, s->slow);
    if (s->enc)
        opus_encoder_destroy(s->enc);
    pthread_mutex_destroy(&s->lock);
    free(s->path);
    free(s);
}
