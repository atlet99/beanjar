// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libkadaptor.so for the emulator on M2 (W16R), beside kadaptor-xc.c: the DVR's secure key.
 *
 * tvservice's DASY (the recording middleware) starts its storage-manager task, then reads the
 * DVR key: DASY_AES_InitKey, DASY_PVRDEV_GetSecureKey, ..., the board HAL's
 * HAL_PVR_CRYPTO_GetSecureKey, which asks KADP for 16 bytes with
 * MAdp_Secure_GetSecureDataEMMC("/tmp/dvr_key", buf, &len). On the TV that item is factory data
 * in the sedata and sestore partitions, which MStar's TEE decrypts (the function copies the item
 * to /tmp/dvr_key from its shared-memory table and reads it back through TeecReadSFile); nothing
 * makes it on a set that lacks it. The VM has no MStar TEE, so the read failed at every boot,
 * DASY's initialisation failed ("dasy initialize fail": no time shift or recording), and it
 * deleted the storage-manager task it had just started. That teardown can free the task's
 * context while the task still runs (LG's task writes its state outside the mutex), and the
 * task then crashed tvservice on a null context at boot, now and then
 * (docs/research/dasy-dvr-key.md).
 *
 * The VM's own DVR key is an item of its secure store (host/core/beanjar/sestore.py, "dvr_key":
 * 16 random bytes made for each VM), which this function answers for that name as KADP's does
 * on the TV: TRUE, the 16 bytes and their length. Every other item is the stock function's (which
 * still fails without the TEE), and so is every other symbol but kadaptor-xc.c's. The board HAL
 * calls it through its PLT, so this definition is the one it reaches. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include "../../../../../include/webos-sestore-items.h"
#include "../sestore-item.h"

#define DVR_KEY_NAME  "/tmp/dvr_key"
#define DVR_KEY_BYTES 16

int MAdp_Secure_GetSecureDataEMMC(const char *name, void *buf, uint32_t *len)
{
    static int (*stock)(const char *, void *, uint32_t *);
    if (name && !strcmp(name, DVR_KEY_NAME)) {
        unsigned char key[DVR_KEY_BYTES];
        unsigned int size = 0;
        if (!buf || !len || *len < DVR_KEY_BYTES ||
            emulator_sestore_read(SESTORE_ITEM_DVR_KEY, key, &size, sizeof key) ||
            size != DVR_KEY_BYTES)
            return 0;
        memcpy(buf, key, DVR_KEY_BYTES);
        *len = DVR_KEY_BYTES;
        return 1;
    }
    if (!stock)
        stock = (int (*)(const char *, void *, uint32_t *))dlsym(RTLD_NEXT, __func__);
    return stock ? stock(name, buf, len) : 0;
}
