// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* O22N2's supplied KMS integration omits the Starfish integration's input
 * startup. Reuse the supplied IM-to-Qt manager once the application's Qt
 * objects exist. It owns callbacks, input policy, workers and event delivery.
 * No input device is read or synthesized here.
 *
 * Verified in 33.20.23 libStarfishInput.so.0.0.1: instance() 0x102b8,
 * startInputService() 0xcc08; the stock Starfish plugin calls these together.
 * surface-manager imports QGuiApplication::exec(), our initialization boundary.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXPORT          __attribute__((visibility("default")))
#define EXEC_SYMBOL     "_ZN15QGuiApplication4execEv"
#define INSTANCE_SYMBOL "_ZN21QStarfishInputManager8instanceEv"
#define START_SYMBOL    "_ZN21QStarfishInputManager17startInputServiceEv"

static int enabled;
static pthread_once_t startup_once = PTHREAD_ONCE_INIT;
static pthread_once_t exec_once = PTHREAD_ONCE_INIT;
static int (*real_exec)(void);
/* Never unload: IM workers and Qt connections can retain library callbacks. */
static void *input_module;
static void *qt_module;

static void remove_self_preload(void)
{
    const char *value = getenv("LD_PRELOAD");
    if (!value)
        return;
    char *input = strdup(value), *output = calloc(strlen(value) + 1, 1);
    if (!input || !output) {
        free(input);
        free(output);
        return;
    }
    char *save = NULL;
    for (char *word = strtok_r(input, " :\t\n", &save); word;
         word = strtok_r(NULL, " :\t\n", &save)) {
        const char *base = strrchr(word, '/');
        if (!strcmp(base ? base + 1 : word, "libkms-native-input.so"))
            continue;
        if (*output)
            strcat(output, " ");
        strcat(output, word);
    }
    if (*output)
        setenv("LD_PRELOAD", output, 1);
    else
        unsetenv("LD_PRELOAD");
    free(input);
    free(output);
}

__attribute__((constructor)) static void initialize(void)
{
    int saved_errno = errno;
    const char *flag = getenv("BEANJAR_NATIVE_INPUT");
    char path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (flag && !strcmp(flag, "1") && length >= 0 && (size_t)length < sizeof(path) - 1) {
        path[length] = 0;
        const char *base = strrchr(path, '/');
        if (!strcmp(base ? base + 1 : path, "surface-manager")) {
            enabled = 1;
            remove_self_preload();
        }
    }
    errno = saved_errno;
}

static void resolve_exec(void)
{
    real_exec = (int (*)(void))dlsym(RTLD_NEXT, EXEC_SYMBOL);
}

static void start_stock_input(void)
{
    input_module = dlopen("libStarfishInput.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!input_module) {
        fprintf(stderr, "kms-native-input: cannot load stock input: %s\n", dlerror());
        return;
    }
    void *(*instance)(void) = (void *(*)(void))dlsym(input_module, INSTANCE_SYMBOL);
    void (*start)(void *) = (void (*)(void *))dlsym(input_module, START_SYMBOL);
    if (!instance || !start) {
        fprintf(stderr, "kms-native-input: stock input manager exports missing\n");
        return;
    }
    /* Keep Qt dependencies out of the preload's disabled/shell scope. The
     * companion uses real Qt objects and the active native resource API. */
    qt_module = dlopen("libkms-native-input-qt.so", RTLD_NOW | RTLD_LOCAL);
    if (!qt_module) {
        fprintf(stderr, "kms-native-input: cannot load cursor binding: %s\n", dlerror());
    } else {
        void (*bind_cursor)(void *) =
            (void (*)(void *))dlsym(qt_module, "beanjar_native_input_bind_cursor");
        if (bind_cursor)
            bind_cursor(input_module);
        else
            fprintf(stderr, "kms-native-input: cursor binding export missing\n");
    }
    void *manager = instance();
    if (!manager) {
        fprintf(stderr, "kms-native-input: stock input manager unavailable\n");
        return;
    }
    start(manager);
    /* The vendor method returns void: this confirms invocation, not that
     * devices, service subscriptions or cursor rendering have succeeded. */
    fprintf(stderr, "kms-native-input: invoked stock input service startup\n");
}

EXPORT int native_gui_exec(void) __asm__(EXEC_SYMBOL);
int native_gui_exec(void)
{
    int saved_errno = errno;
    pthread_once(&exec_once, resolve_exec);
    if (!real_exec) {
        fprintf(stderr, "kms-native-input: real QGuiApplication::exec missing\n");
        errno = ENOSYS;
        return -1;
    }
    if (enabled)
        pthread_once(&startup_once, start_stock_input);
    errno = saved_errno;
    return real_exec();
}
