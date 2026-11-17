// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libhal_libs.so for the emulator on MStar's LM21 family: the DVR's secure key.
 *
 * The recording middleware (DASY in legacy-broadcast, ts-record's aesInitKey) reads the DVR key
 * with the board HAL's HAL_PVR_CRYPTO_GetSecureKey(?, buf), which asks KADP for 16 bytes with
 * MAdp_Secure_GetSecureDataEMMC("/tmp/96B14BF8", buf, &len): factory data in the sedata and
 * sestore partitions that MStar's TEE decrypts on the TV. The VM has no MStar TEE, so the read
 * failed where a set's succeeds. On M2 and M3 the libkadaptor shim answers the KADP call
 * (src/libkadaptor/kadaptor-pvrkey.c); on LM21 KADP's file is libkadaptor.so itself, the name such
 * a shim would take, so the answer is given here, one level up: 0 and the 16 bytes of the VM's
 * store item "dvr_key" (host/core/beanjar/sestore.py), or -1, the stock failure, without it.
 * Every other symbol is the stock library's, which this one NEEDs by its full name. */
#include <string.h>
#include "../../../../../include/webos-sestore-items.h"
#include "../sestore-item.h"

#define DVR_KEY_BYTES 16

int HAL_PVR_CRYPTO_GetSecureKey(int unused, unsigned char *key)
{
    unsigned char value[DVR_KEY_BYTES];
    unsigned int size = 0;
    (void)unused;
    if (!key || emulator_sestore_read(SESTORE_ITEM_DVR_KEY, value, &size, sizeof value) ||
        size != DVR_KEY_BYTES)
        return -1;
    memcpy(key, value, DVR_KEY_BYTES);
    return 0;
}
