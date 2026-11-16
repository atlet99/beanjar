// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The media helper's bounded logs (host/media/log.c): guest strings made printable and cut, a file
 * rotated to one older file at its cap, lines rate-limited per site and port with the count of what
 * was dropped written once the site is quiet, and a write past RLIMIT_FSIZE failing (EFBIG) into a
 * rotation rather than SIGXFSZ. Prints TAP; exit status 0 only if every check passed. */
#define _GNU_SOURCE
#include "../../../host/media/log.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

static char dir[] = "/tmp/check-media-log-XXXXXX";
static int64_t fake_now;
static int64_t fake_clock(void)
{
    return fake_now;
}

static char *slurp(const char *path, size_t *len)
{
    static char buf[1 << 20];
    FILE *f = fopen(path, "rb");
    *len = 0;
    if (!f) {
        buf[0] = 0;
        return buf;
    }
    *len = fread(buf, 1, sizeof buf - 1, f);
    buf[*len] = 0;
    fclose(f);
    return buf;
}

static long fsize(const char *path)
{
    struct stat st;
    return stat(path, &st) ? -1 : (long)st.st_size;
}

static unsigned count(const char *text, const char *needle)
{
    unsigned c = 0;
    for (const char *p = text; (p = strstr(p, needle)); p += strlen(needle))
        c++;
    return c;
}

static void printable(void)
{
    char out[64];
    const char evil[] = "vdec\x1b[2J\r\nfake 2024\x7f\xff!";
    hm_log_printable(out, sizeof out, evil, sizeof evil - 1);
    ok(!strcmp(out, "vdec\\x1b[2J\\x0d\\x0afake 2024\\x7f\\xff!"), "control bytes escaped: %s",
       out);
    hm_log_printable(out, sizeof out, "abc\0def", 7);
    ok(!strcmp(out, "abc"), "stops at a NUL: %s", out);
    char longer[200];
    memset(longer, 'x', sizeof longer);
    size_t len = hm_log_printable(out, 16, longer, sizeof longer);
    ok(len == 15 && !strcmp(out, "xxxxxxxxxxxx..."), "cut to fit with an ellipsis: %s", out);
    len = hm_log_printable(out, 16, "\n\n\n\n\n", 5);
    ok(len <= 15 && strlen(out) == len && !strchr(out, '\n'), "escapes cut whole: %s", out);
    hm_log_printable(out, 16, "exactly fifteen", 15);
    ok(!strcmp(out, "exactly fifteen"), "a string that fits is whole: %s", out);
}

static void lines(void)
{
    char path[256];
    snprintf(path, sizeof path, "%s/lines.log", dir);
    struct hm_log l = HM_LOG_INIT(0);
    l.clock = fake_clock;
    ok(!hm_log_open(&l, path), "opened %s", path);
    hm_logf(&l, 0, "media %d: open %s", 0, "evil\nmedia 1: forged\x1b[31m");
    size_t len;
    char *text = slurp(path, &len);
    ok(!strcmp(text, "media 0: open evil\\x0amedia 1: forged\\x1b[31m\n"),
       "one line for a guest string with a newline: %s", text);
    char big[3 * HM_LOG_LINE];
    memset(big, 'y', sizeof big - 1);
    big[sizeof big - 1] = 0;
    hm_logf(&l, 0, "caps %s", big);
    text = slurp(path, &len);
    const char *second = strchr(text, '\n') + 1;
    ok(strlen(second) == HM_LOG_LINE + 1 && second[HM_LOG_LINE] == '\n',
       "a long line cut to %u bytes", HM_LOG_LINE);
    hm_log_close(&l);
    struct hm_log stamped = HM_LOG_INIT(1);
    hm_log_open(&stamped, path);
    hm_logf(&stamped, -1, "hub: %d", 1);
    hm_log_close(&stamped);
    text = slurp(path, &len);
    const char *last = text + len - 1;
    while (last > text && last[-1] != '\n')
        last--;
    ok(strlen(last) > 25 && last[4] == '-' && last[10] == 'T' && !strcmp(last + 24, " hub: 1\n"),
       "stamped: %s", last);
}

static void rotation(void)
{
    char path[256], old[260], older[260];
    snprintf(path, sizeof path, "%s/rotate.log", dir);
    snprintf(old, sizeof old, "%s.1", path);
    snprintf(older, sizeof older, "%s.2", path);
    struct hm_log l = HM_LOG_INIT(0);
    l.clock = fake_clock;
    l.cap = 1000;
    hm_log_open(&l, path);
    for (int i = 0; i < 400; i++) {
        fake_now += 1000000000; /* one a second: never limited */
        hm_logf(&l, 0, "line %04d of the rotation check", i);
    }
    long now = fsize(path), before = fsize(old);
    ok(now > 0 && now <= 1000, "the log is under its cap: %ld bytes", now);
    ok(before > 900 && before <= 1000, "the one before is kept: %ld bytes", before);
    ok(fsize(older) < 0, "and no older one");
    size_t len;
    char *text = slurp(path, &len);
    ok(strstr(text, "line 0399 of") != NULL, "the newest line is in the log");
    text = slurp(old, &len);
    ok(strstr(text, "line 0399") == NULL && strstr(text, "line 0000") == NULL,
       "the old file has the lines just before");
    ok(l.failed == 0, "no line lost");
    /* a log reopened on a file over its cap rotates at its first line */
    hm_log_close(&l);
    struct hm_log again = HM_LOG_INIT(0);
    again.cap = 10;
    hm_log_open(&again, path);
    hm_logf(&again, 0, "after a restart");
    hm_log_close(&again);
    text = slurp(path, &len);
    ok(!strcmp(text, "after a restart\n"), "a restart past the cap rotates: %s", text);
}

static void limits(void)
{
    char path[256];
    snprintf(path, sizeof path, "%s/limit.log", dir);
    struct hm_log l = HM_LOG_INIT(0);
    l.clock = fake_clock;
    hm_log_open(&l, path);
    fake_now = 1000000000000;
    /* a guest looping FLUSH on port 0: the burst, then nothing */
    for (int i = 0; i < 1000; i++)
        hm_logf(&l, 0, "media %d: flush %s", 0, "video");
    /* port 1 and another site of port 0 keep their own buckets */
    hm_logf(&l, 1, "media %d: flush %s", 1, "video");
    hm_logf(&l, 0, "media %d: close %s", 0, "video");
    size_t len;
    char *text = slurp(path, &len);
    ok(count(text, "media 0: flush") == (unsigned)HM_LOG_BURST, "%u of 1000 lines in a burst",
       count(text, "media 0: flush"));
    ok(count(text, "media 1: flush") == 1 && count(text, "media 0: close") == 1,
       "other ports and sites are not limited by it");
    ok(!strstr(text, "suppressed"), "nothing about it while it goes on");
    hm_log_tick(&l);
    text = slurp(path, &len);
    ok(!strstr(text, "suppressed"), "nor at a tick with no token back");
    fake_now += 1000000000; /* HM_LOG_RATE tokens back */
    hm_log_tick(&l);
    text = slurp(path, &len);
    ok(strstr(text, "log: 950 lines of port 0 suppressed: media %d: flush %s\n") != NULL,
       "one line counts what was dropped, at the next tick");
    hm_log_tick(&l);
    ok(count(slurp(path, &len), "suppressed") == 1, "once");
    /* a flood that stops by itself: the count comes before its next line */
    for (int i = 0; i < 100; i++)
        hm_logf(&l, 2, "media %d: state", 2);
    fake_now += 2000000000;
    hm_logf(&l, 2, "media %d: state", 2);
    text = slurp(path, &len);
    const char *note = strstr(text, "log: 50 lines of port 2 suppressed: media %d: state\n");
    ok(note && !strcmp(note + strlen("log: 50 lines of port 2 suppressed: media %d: state\n"),
                       "media 2: state\n"),
       "or before the site's next line");
    /* sustained: the rate */
    unsigned before = count(slurp(path, &len), "media 3: eos");
    for (int i = 0; i < 10000; i++) {
        fake_now += 10000000; /* 100 a second for 100 s */
        hm_logf(&l, 3, "media %d: eos", 3);
    }
    unsigned written = count(slurp(path, &len), "media 3: eos") - before;
    ok(written >= HM_LOG_BURST + 100 * HM_LOG_RATE - 2 &&
           written <= HM_LOG_BURST + 100 * HM_LOG_RATE + 2,
       "a sustained flood gets the rate: %u lines in 100 s", written);
    hm_log_close(&l);
}

/* a child: RLIMIT_FSIZE below the cap, as a helper whose limit a log reaches first */
static void fsize_limit(void)
{
    char path[256], old[260];
    snprintf(path, sizeof path, "%s/fsize.log", dir);
    snprintf(old, sizeof old, "%s.1", path);
    fflush(stdout);
    pid_t pid = fork();
    if (!pid) {
        signal(SIGXFSZ, SIG_IGN);
        struct rlimit r = {4096, 4096};
        setrlimit(RLIMIT_FSIZE, &r);
        struct hm_log l = HM_LOG_INIT(0);
        l.clock = fake_clock;
        l.cap = 1 << 20;
        hm_log_open(&l, path);
        for (int i = 0; i < 300; i++) {
            fake_now += 1000000000;
            hm_logf(&l, 0, "line %04d past the file size limit", i);
        }
        hm_log_close(&l);
        _exit(l.failed ? 3 : 0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    ok(WIFEXITED(status), "a write past RLIMIT_FSIZE does not kill (status %#x)", status);
    ok(WIFEXITED(status) && WEXITSTATUS(status) == 0, "it rotates and loses no line");
    ok(fsize(path) <= 4096 && fsize(old) <= 4096 && fsize(old) > 3000, "both files under it");
}

int main(void)
{
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 2;
    }
    printable();
    lines();
    rotation();
    limits();
    fsize_limit();
    printf("1..%d\n", n);
    char cmd[64];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd)) {
        /* left behind: a small directory in /tmp */
    }
    return failed ? 1 : 0;
}
