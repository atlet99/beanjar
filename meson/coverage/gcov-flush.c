// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Linked into media-helper in a coverage build only (-Db_coverage=true, meson/coverage). gcov
 * writes a program's counters when it exits, and SIGTERM's default action, which is how the
 * launcher and the tests stop media-helper, ends it without that. Here SIGTERM and SIGINT are
 * blocked before main, so every thread inherits that, and a thread of their own waits for them:
 * it writes the counters and ends the process with the signal, as before. Not in a signal
 * handler: gcov takes locks a decoder thread may hold. Not for a program with handlers of its
 * own (beanjar-relay, beanjar-vnc, viewer-headless), which exit by themselves. */
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>

extern void __gcov_dump(void);

static sigset_t sigs;

static void *waiter(void *arg)
{
    (void)arg;
    int sig;
    if (sigwait(&sigs, &sig) != 0)
        return NULL;
    __gcov_dump();
    signal(sig, SIG_DFL);
    pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);
    raise(sig);
    return NULL;
}

__attribute__((constructor)) static void install(void)
{
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGTERM);
    sigaddset(&sigs, SIGINT);
    pthread_t t;
    if (pthread_sigmask(SIG_BLOCK, &sigs, NULL) == 0 && pthread_create(&t, NULL, waiter, NULL) == 0)
        pthread_detach(t);
}
