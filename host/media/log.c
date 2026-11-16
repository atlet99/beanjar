// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The media helper's bounded logs (log.h): printable lines, rotation at a cap, a token bucket per
 * call site and port. */
#define _GNU_SOURCE
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct hm_log hm_engine_log = HM_LOG_INIT(0);

static int64_t now_ns(const struct hm_log *l)
{
    if (l->clock)
        return l->clock();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static size_t width(unsigned char c)
{
    return c >= 0x20 && c < 0x7f ? 1 : 4; /* as itself, or \xNN */
}

size_t hm_log_printable(char *dst, size_t cap, const char *src, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    size_t out = 0;
    if (!cap)
        return 0;
    for (size_t i = 0; i < len && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        size_t need = width(c);
        /* room for "..." stays free unless the rest fits whole */
        if (out + need + 3 > cap - 1) {
            size_t rest = 0;
            for (size_t j = i; j < len && src[j] && out + rest <= cap - 1; j++)
                rest += width((unsigned char)src[j]);
            if (out + rest > cap - 1) {
                if (out + 3 <= cap - 1) {
                    memcpy(dst + out, "...", 3);
                    out += 3;
                }
                break;
            }
        }
        if (need == 1)
            dst[out++] = (char)c;
        else {
            dst[out++] = '\\';
            dst[out++] = 'x';
            dst[out++] = hex[c >> 4];
            dst[out++] = hex[c & 15];
        }
    }
    dst[out] = 0;
    return out;
}

/* with l->lock: a file at path again (after a rotation that could not open it, or a failed open) */
static int reopen(struct hm_log *l)
{
    int fd = open(l->path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd < 0)
        return -1;
    struct stat st;
    l->size = fstat(fd, &st) ? 0 : (uint64_t)st.st_size;
    l->fd = fd;
    return 0;
}

/* with l->lock: path to path.1 (the one before goes), and a new path */
static void rotate(struct hm_log *l)
{
    char old[sizeof l->path + 2];
    snprintf(old, sizeof old, "%s.1", l->path);
    rename(l->path, old);
    if (l->fd >= 0)
        close(l->fd);
    l->fd = -1;
    l->size = 0;
    reopen(l);
}

/* with l->lock: one whole line (text and its newline) */
static void put(struct hm_log *l, const char *line, size_t n)
{
    if (l->path[0]) {
        if (l->fd < 0 && reopen(l) < 0) {
            l->failed++;
            return;
        }
        if (l->size && l->size + n > l->cap)
            rotate(l);
        if (l->fd < 0) {
            l->failed++;
            return;
        }
    }
    int fd = l->path[0] ? l->fd : STDERR_FILENO;
    int rotated = 0;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, line + off, n - off);
        if (w > 0) {
            off += (size_t)w;
            l->size += (uint64_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR)
            continue;
        /* EFBIG (the helper's RLIMIT_FSIZE, SIGXFSZ ignored): the line again, whole, in a new
         * file (the old one may end with the part that fitted) */
        if (w < 0 && errno == EFBIG && l->path[0] && !rotated) {
            rotated = 1;
            rotate(l);
            off = 0;
            if ((fd = l->fd) >= 0)
                continue;
        }
        l->failed++;
        return;
    }
}

/* with l->lock: text (printable already) as a line, after the time when the log is stamped */
static void emit(struct hm_log *l, const char *text)
{
    char line[48 + HM_LOG_LINE + 2];
    size_t n = 0;
    if (l->stamp) {
        struct timespec ts;
        struct tm tm;
        clock_gettime(CLOCK_REALTIME, &ts);
        gmtime_r(&ts.tv_sec, &tm);
        n = strftime(line, 32, "%Y-%m-%dT%H:%M:%S", &tm);
        n += (size_t)snprintf(line + n, sizeof line - n, ".%03ldZ ", ts.tv_nsec / 1000000);
    }
    size_t t = strlen(text);
    if (t > HM_LOG_LINE)
        t = HM_LOG_LINE;
    memcpy(line + n, text, t);
    n += t;
    line[n++] = '\n';
    put(l, line, n);
}

/* with l->lock: the count of what site dropped, as a line */
static void note(struct hm_log *l, struct hm_log_site *s)
{
    char fmt[96], text[HM_LOG_LINE + 1];
    hm_log_printable(fmt, sizeof fmt, s->fmt, strlen(s->fmt));
    if (s->port >= 0)
        snprintf(text, sizeof text, "log: %lu lines of port %d suppressed: %s", s->suppressed,
                 s->port, fmt);
    else
        snprintf(text, sizeof text, "log: %lu lines suppressed: %s", s->suppressed, fmt);
    s->suppressed = 0;
    emit(l, text);
}

static void refill(struct hm_log_site *s, int64_t now)
{
    if (now > s->last) {
        s->tokens += (double)(now - s->last) / 1e9 * HM_LOG_RATE;
        if (s->tokens > HM_LOG_BURST)
            s->tokens = HM_LOG_BURST;
    }
    s->last = now;
}

/* with l->lock: the bucket of (fmt, port); one shared by several when the table is full */
static struct hm_log_site *site(struct hm_log *l, const char *fmt, int port, int64_t now)
{
    uintptr_t h = ((uintptr_t)fmt >> 3) ^ ((uintptr_t)(unsigned)port * 2654435761u);
    for (unsigned i = 0; i < HM_LOG_SITES; i++) {
        struct hm_log_site *s = &l->site[(h + i) % HM_LOG_SITES];
        if (s->fmt == fmt && s->port == port)
            return s;
        if (!s->fmt) {
            *s =
                (struct hm_log_site){.fmt = fmt, .port = port, .tokens = HM_LOG_BURST, .last = now};
            return s;
        }
    }
    return &l->site[h % HM_LOG_SITES];
}

void hm_logkv(struct hm_log *l, int port, const char *key, const char *fmt, va_list ap)
{
    char raw[2 * HM_LOG_LINE], text[HM_LOG_LINE + 1];
    int r = vsnprintf(raw, sizeof raw, fmt, ap);
    size_t n = r < 0 ? 0 : (size_t)r < sizeof raw ? (size_t)r : sizeof raw - 1;
    while (n && raw[n - 1] == '\n') /* FFmpeg's lines end with one */
        n--;
    hm_log_printable(text, sizeof text, raw, n);
    pthread_mutex_lock(&l->lock);
    int64_t now = now_ns(l);
    struct hm_log_site *s = site(l, key, port, now);
    refill(s, now);
    if (s->tokens < 1) {
        s->suppressed++;
    } else {
        s->tokens -= 1;
        if (s->suppressed)
            note(l, s);
        emit(l, text);
    }
    pthread_mutex_unlock(&l->lock);
}

void hm_logv(struct hm_log *l, int port, const char *fmt, va_list ap)
{
    hm_logkv(l, port, fmt, fmt, ap);
}

void hm_logf(struct hm_log *l, int port, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    hm_logv(l, port, fmt, ap);
    va_end(ap);
}

void hm_log_tick(struct hm_log *l)
{
    pthread_mutex_lock(&l->lock);
    int64_t now = now_ns(l);
    for (unsigned i = 0; i < HM_LOG_SITES; i++) {
        struct hm_log_site *s = &l->site[i];
        if (!s->fmt || !s->suppressed)
            continue;
        refill(s, now);
        if (s->tokens >= 1)
            note(l, s);
    }
    pthread_mutex_unlock(&l->lock);
}

int hm_log_open(struct hm_log *l, const char *path)
{
    if (strlen(path) >= sizeof l->path) {
        errno = ENAMETOOLONG;
        return -1;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd < 0)
        return -1;
    struct stat st;
    pthread_mutex_lock(&l->lock);
    if (l->path[0] && l->fd >= 0)
        close(l->fd);
    snprintf(l->path, sizeof l->path, "%s", path);
    l->fd = fd;
    l->size = fstat(fd, &st) ? 0 : (uint64_t)st.st_size;
    pthread_mutex_unlock(&l->lock);
    return 0;
}

void hm_log_close(struct hm_log *l)
{
    pthread_mutex_lock(&l->lock);
    if (l->path[0] && l->fd >= 0)
        close(l->fd);
    l->path[0] = 0;
    l->fd = -1;
    pthread_mutex_unlock(&l->lock);
}
