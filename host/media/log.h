// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef HOST_MEDIA_LOG_H
#define HOST_MEDIA_LOG_H
/* The media helper's logs: what a guest can make it write is bounded.
 *
 * A log is a file the helper owns (or stderr, when it was given none: the tests and ad-hoc runs).
 * Each line is formatted, made printable (control characters, DEL and bytes above 0x7e as \xNN:
 * a guest's element name or caps cannot end a line or move a terminal's cursor) and cut to
 * HM_LOG_LINE bytes. A file past its cap is renamed to <path>.1, replacing the one before, and
 * started again, so one log takes at most twice its cap on disk.
 *
 * Lines are rate-limited per call site and port: a token bucket of HM_LOG_BURST lines refilled at
 * HM_LOG_RATE a second for each (format string, port). A guest looping FLUSH or a corrupt stream
 * making FFmpeg complain on every packet costs a few lines a second; what is dropped is counted
 * and written as one "suppressed" line once the bucket has a token again (the next line of that
 * site, or hm_log_tick). Thread-safe. */
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define HM_LOG_CAP   (4u << 20) /* bytes in a log file before it is rotated */
#define HM_LOG_LINE  1024       /* bytes of a line, made printable, without time and newline */
#define HM_LOG_BURST 50.0       /* lines a site may write at once */
#define HM_LOG_RATE  5.0        /* lines a second a site gets back */
#define HM_LOG_SITES 256

struct hm_log_site {
    const char *fmt; /* NULL: free */
    int port;
    double tokens;
    int64_t last;
    unsigned long suppressed;
};

struct hm_log {
    pthread_mutex_t lock;
    int fd;          /* -1: stderr (no path), or the file to reopen (path) */
    char path[4096]; /* empty: stderr, never rotated */
    uint64_t size, cap;
    int stamp;                 /* lines start with the UTC time, ISO 8601 with milliseconds */
    int64_t (*clock)(void);    /* ns, monotonic; NULL: CLOCK_MONOTONIC (tests set their own) */
    unsigned long long failed; /* lines not written (full disk, RLIMIT_FSIZE, no file) */
    struct hm_log_site site[HM_LOG_SITES];
};

#define HM_LOG_INIT(stamped)                                                                       \
    {.lock = PTHREAD_MUTEX_INITIALIZER, .fd = -1, .cap = HM_LOG_CAP, .stamp = (stamped)}

/* The decode engine's log (host/media/decode/): stderr until the helper opens a file for it. */
extern struct hm_log hm_engine_log;

/* Lines go to path from now on (appended, created 0666 less the umask). 0, or -1 with errno and
 * the log unchanged. */
int hm_log_open(struct hm_log *l, const char *path);
void hm_log_close(struct hm_log *l);
/* One line; port is the guest's media port, or -1 for the helper's own. */
void hm_logf(struct hm_log *l, int port, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void hm_logv(struct hm_log *l, int port, const char *fmt, va_list ap)
    __attribute__((format(printf, 3, 0)));
/* As hm_logv, limited as the site key (a string that lives as long as the log: FFmpeg's format) */
void hm_logkv(struct hm_log *l, int port, const char *key, const char *fmt, va_list ap)
    __attribute__((format(printf, 4, 0)));
/* The "suppressed" lines of sites quiet since: the helper's loop calls it about once a second. */
void hm_log_tick(struct hm_log *l);
/* len bytes of src (up to a NUL) into dst made printable, cut to fit cap with "..."; dst is always
 * terminated (cap > 0). The length written. */
size_t hm_log_printable(char *dst, size_t cap, const char *src, size_t len);

#endif
