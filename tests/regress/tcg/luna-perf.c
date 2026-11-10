// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Repeated requests over one LS2 connection, using the firmware's real libraries. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ls2-compat.h"
extern bool LSCallOneReply(LSHandle *, const char *, const char *, LSMethodFunction, void *,
                           unsigned long *, LSError *);
extern void g_main_loop_quit(GMainLoop *);
extern unsigned g_timeout_add_seconds(unsigned, int (*)(void *), void *);
static GMainLoop *loop;
static unsigned count;
static int failed;
static double start;
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static void next(LSHandle *sh);
static bool reply(LSHandle *sh, LSMessage *msg, void *ctx)
{
    (void)ctx;
    const char *body = LSMessageGetPayload(msg);
    /* LG's tvpowerd answers through json-c: "state": "Active" */
    if (!body ||
        (!strstr(body, "\"state\":\"Active\"") && !strstr(body, "\"state\": \"Active\""))) {
        fprintf(stderr, "unexpected reply: %s\n", body ? body : "(null)");
        failed = 1;
        g_main_loop_quit(loop);
        return true;
    }
    count++;
    if (count == 10)
        start = now(); /* warm connection and code */
    if (count == 1010) {
        printf("persistent1000 seconds=%.6f\n", now() - start);
        g_main_loop_quit(loop);
    } else
        next(sh);
    return true;
}
static void next(LSHandle *sh)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSCallOneReply(sh, "luna://com.webos.service.tvpower/power/getPowerState", "{}", reply,
                        NULL, NULL, &error)) {
        LSErrorPrint(&error, stderr);
        failed = 1;
        g_main_loop_quit(loop);
    }
    LSErrorFree(&error);
}
static int expired(void *ctx)
{
    (void)ctx;
    failed = 1;
    g_main_loop_quit(loop);
    return 0;
}
/* Older LS2 has separate buses; keep this client on its private bus. Queue's
 * unified LS2 exports LSRegister alone (the installed SDK declares that API). */
static bool register_private(const char *name, LSHandle **sh, LSError *error)
{
    bool (*legacy)(const char *, LSHandle **, bool, LSError *) =
        dlsym(RTLD_DEFAULT, "LSRegisterPubPriv");
    if (legacy)
        return legacy(name, sh, false, error);
    bool (*unified)(const char *, LSHandle **, LSError *) = dlsym(RTLD_DEFAULT, "LSRegister");
    if (unified)
        return unified(name, sh, error);
    fprintf(stderr, "LS2 has no registration API\n");
    return false;
}
int main(void)
{
    LSHandle *sh = NULL;
    LSError error;
    LSErrorInit(&error);
    loop = g_main_loop_new(NULL, false);
    if (!register_private("com.webos.emulator.tcgperf", &sh, &error) ||
        !LSGmainAttach(sh, loop, &error)) {
        LSErrorPrint(&error, stderr);
        return 1;
    }
    g_timeout_add_seconds(30, expired, NULL);
    next(sh);
    if (!failed)
        g_main_loop_run(loop);
    return failed || count != 1010;
}
