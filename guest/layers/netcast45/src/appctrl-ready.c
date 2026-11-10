// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Delay only stock appctrl's synchronous readiness call. Registration and
 * receiver startup stay in their original order. The SDK has no libdbus
 * headers: bus_api uses its public opaque-pointer/scalar ABI. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

struct bus_api {
    void *(*get_private)(int, void *);
    void *(*new_call)(const char *, const char *, const char *, const char *);
    void *(*send)(void *, void *, int, void *);
    int (*append)(void *, int, ...);
    int (*get_args)(void *, void *, int, ...);
    int (*connected)(void *);
    void (*exit_on_disconnect)(void *, int);
    void (*message_unref)(void *);
    void (*close)(void *);
    void (*unref)(void *);
};

#define LOAD(field, symbol)                                                                        \
    do {                                                                                           \
        *(void **)(&api.field) = dlsym(library, symbol);                                           \
        if (!api.field) {                                                                          \
            fprintf(stderr, "appctrl-ready: missing %s\n", symbol);                                \
            goto out;                                                                              \
        }                                                                                          \
    } while (0)

static int wait_for_release(void)
{
    struct bus_api api = {0};
    void *library = NULL, *bus = NULL, *message = NULL, *reply = NULL;
    const char *target = "lg.service.special.broadcast";
    const char *caller = "lg.service.special.appctrl";
    int32_t caller_pid = (int32_t)getpid();
    int ready = 0;

    library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "appctrl-ready: cannot load libdbus-1.so.3\n");
        goto out;
    }
    LOAD(get_private, "dbus_bus_get_private");
    LOAD(new_call, "dbus_message_new_method_call");
    LOAD(send, "dbus_connection_send_with_reply_and_block");
    LOAD(append, "dbus_message_append_args");
    LOAD(get_args, "dbus_message_get_args");
    LOAD(connected, "dbus_connection_get_is_connected");
    LOAD(exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
    LOAD(message_unref, "dbus_message_unref");
    LOAD(close, "dbus_connection_close");
    LOAD(unref, "dbus_connection_unref");
    bus = api.get_private(0 /* DBUS_BUS_SESSION */, NULL);
    if (!bus) {
        fprintf(stderr, "appctrl-ready: session bus unavailable\n");
        goto out;
    }
    api.exit_on_disconnect(bus, 0);
    while (api.connected(bus)) {
        uint32_t peer_pid = 0, state = 0;
        int registered = 0, valid;
        char payload[40];
        const char *data = payload;
        const struct timespec pause = {0, 100000000};

        message = api.new_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                               "org.freedesktop.DBus", "GetConnectionUnixProcessID");
        if (!message || !api.append(message, 's', &target, 0))
            goto out;
        reply = api.send(bus, message, 2000, NULL);
        api.message_unref(message);
        message = NULL;
        valid = reply && api.get_args(reply, NULL, 'u', &peer_pid, 0);
        if (reply)
            api.message_unref(reply);
        reply = NULL;
        if (valid && peer_pid) {
            snprintf(payload, sizeof(payload), "<#*/i/*#>%x", (unsigned)peer_pid);
            message = api.new_call("lg.service.special.processmanager", "/hoa/proc",
                                   "lg.if.service", "HOA_PROC_GetProcessState");
            if (!message || !api.append(message, 'i', &caller_pid, 's', &caller, 's', &data, 0))
                goto out;
            reply = api.send(bus, message, 2000, NULL);
            api.message_unref(message);
            message = NULL;
            valid = reply && api.get_args(reply, NULL, 'u', &state, 'b', &registered, 0);
            if (reply)
                api.message_unref(reply);
            reply = NULL;
            /* Lifecycle states, not GetProcessStatus's Linux process state. */
            if (valid && registered && (state == 0x20 || state == 0x41 || state == 0x42)) {
                fprintf(stderr, "appctrl-ready: RELEASE registered, state=0x%x\n", (unsigned)state);
                ready = 1;
                break;
            }
        }
        nanosleep(&pause, NULL);
    }
out:
    if (reply)
        api.message_unref(reply);
    if (message)
        api.message_unref(message);
    if (bus) {
        api.close(bus);
        api.unref(bus);
    }
    if (library)
        dlclose(library);
    if (!ready) {
        fprintf(stderr, "appctrl-ready: RELEASE registration unavailable\n");
        return 1;
    }
    return 0;
}

/* The launcher token belongs to this exec, never to forked or execed children.
 * Restore the original preload environment after the loader has loaded us. */
static pid_t owner_pid;

__attribute__((constructor)) static void scope_preload(void)
{
    const char *token = getenv("BEANJAR_APPCTRL_READY_PID");
    const char *saved = getenv("BEANJAR_APPCTRL_PRELOAD");
    char expected[32];
    if (!token)
        return;
    snprintf(expected, sizeof(expected), "%ld", (long)getpid());
    if (strcmp(token, expected)) {
        fprintf(stderr, "appctrl-ready: invalid launcher PID marker\n");
        _exit(1);
    }
    owner_pid = getpid();
    if ((saved ? setenv("LD_PRELOAD", saved, 1) : unsetenv("LD_PRELOAD")) ||
        unsetenv("BEANJAR_APPCTRL_PRELOAD") || unsetenv("BEANJAR_APPCTRL_READY_PID")) {
        fprintf(stderr, "appctrl-ready: cannot restore preload environment\n");
        _exit(1);
    }
}

int HOA_PROC_SetReady(void)
{
    int (*stock_set_ready)(void);
    *(void **)(&stock_set_ready) = dlsym(RTLD_NEXT, "HOA_PROC_SetReady");
    if (!stock_set_ready) {
        fprintf(stderr, "appctrl-ready: stock HOA_PROC_SetReady unavailable\n");
        _exit(1);
    }
    if (owner_pid && owner_pid == getpid()) {
        fprintf(stderr, "appctrl-ready: holding genuine HOA_PROC_SetReady pid=%ld\n",
                (long)getpid());
        if (wait_for_release())
            _exit(1);
        fprintf(stderr, "appctrl-ready: calling stock HOA_PROC_SetReady\n");
    }
    return stock_set_ready();
}
