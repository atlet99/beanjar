// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The stock sestore ABI over the VM's kernel store. Every configured item is an exact read.
 * Missing fallback retains stock-library forwarding and queue's historical plain-model alias
 * for regional requests. Device failures never fall through to stock or synthetic identities. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include "webos-sestore-items.h"
#include "../sestore-item.h"
#define SESTORE_FAIL 0xFFFF0002u

#ifdef SESTORE_MODEL_ITEMS
#include "../model-key.h"
static int is_sdp_model(const char *id)
{
    size_t n = sizeof SESTORE_ITEM_SDP_MODEL - 1;
    if (!id || strncmp(id, SESTORE_ITEM_SDP_MODEL, n))
        return 0;
    return !id[n] || (id[n] == '_' && id[n + 1] &&
                      strchr(SESTORE_ITEM_SDP_MODEL_REGIONS, id[n + 1]) && !id[n + 2]);
}
#endif

int SESTORE_ReadData(const char *id, unsigned char *buf, unsigned int *len)
{
    static int (*stock)(const char *, unsigned char *, unsigned int *);
    int result = emulator_sestore_read(id, buf, len, WEBOS_SESTORE_MAX_VALUE);
    if (!result)
        return 0;
    if (result < 0 || !emulator_sestore_fallback())
        return (int)SESTORE_FAIL;
#ifdef SESTORE_MODEL_ITEMS
    if (is_sdp_model(id)) {
        unsigned char key[44];
        unsigned int size = sizeof key;
        if (!len || emulator_read_model_key(key, &size))
            return (int)SESTORE_FAIL;
        if (buf)
            memcpy(buf, key, size);
        memset(key, 0, sizeof key);
        *len = size;
        return 0;
    }
#endif
    if (!stock)
        stock = (int (*)(const char *, unsigned char *, unsigned int *))dlsym(RTLD_NEXT,
                                                                              "SESTORE_ReadData");
    return stock ? stock(id, buf, len) : (int)SESTORE_FAIL;
}
