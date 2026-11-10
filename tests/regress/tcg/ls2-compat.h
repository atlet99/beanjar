// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A subset of the luna-service2 3.6 C API (libluna-service2.so.3 in the WT1H firmware) and of
 * GLib, for tcg-luna-perf (luna-perf.c). Declared here rather than taken from the SDK sysroot,
 * whose luna-service2 3.21 and GLib 2.82 headers describe newer libraries than the firmware ships.
 * All of these signatures are unchanged between the two versions.
 */
#ifndef LS2_COMPAT_H
#define LS2_COMPAT_H

#include <stdbool.h>

typedef struct _GMainLoop GMainLoop;
typedef struct _GMainContext GMainContext;
GMainLoop *g_main_loop_new(GMainContext *context, int is_running);
void g_main_loop_run(GMainLoop *loop);

typedef struct LSError {
    int error_code;
    char *message;
    const char *file;
    int line;
    const char *func;
    void *padding;
    unsigned long magic;
} LSError;

typedef struct LSHandle LSHandle;
typedef struct LSMessage LSMessage;
typedef bool (*LSMethodFunction)(LSHandle *sh, LSMessage *msg, void *category_context);
typedef unsigned long LSMethodFlags;

typedef struct {
    const char *name;
    LSMethodFunction function;
    LSMethodFlags flags;
} LSMethod;

bool LSErrorInit(LSError *error);
void LSErrorFree(LSError *error);
void LSErrorPrint(LSError *error, void *file); /* FILE * */

bool LSRegisterPubPriv(const char *name, LSHandle **sh, bool public_bus, LSError *error);
bool LSRegisterCategory(LSHandle *sh, const char *category, LSMethod *methods, void *signals,
                        void *properties, LSError *error);
bool LSGmainAttach(LSHandle *sh, GMainLoop *loop, LSError *error);

const char *LSMessageGetPayload(LSMessage *msg);
const char *LSMessageGetMethod(LSMessage *msg);
const char *LSMessageGetSender(LSMessage *msg);
const char *LSMessageGetSenderServiceName(LSMessage *msg);
bool LSMessageIsSubscription(LSMessage *msg);
bool LSMessageReply(LSHandle *sh, LSMessage *msg, const char *payload, LSError *error);

bool LSSubscriptionAdd(LSHandle *sh, const char *key, LSMessage *msg, LSError *error);
typedef struct LSSubscriptionIter LSSubscriptionIter;
bool LSSubscriptionAcquire(LSHandle *sh, const char *key, LSSubscriptionIter **iter,
                           LSError *error);
void LSSubscriptionRelease(LSSubscriptionIter *iter);
bool LSSubscriptionHasNext(LSSubscriptionIter *iter);
LSMessage *LSSubscriptionNext(LSSubscriptionIter *iter);
void LSSubscriptionRemove(LSSubscriptionIter *iter);
bool LSSubscriptionReply(LSHandle *sh, const char *key, const char *payload, LSError *error);

#endif
