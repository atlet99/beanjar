// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* W21P's DILE model-key read goes straight to libsestore, bypassing HAL_CRYPTO. Supply the same
 * per-VM key as the older HAL wrapper, before the hardware-backed secure-store initialization.
 * Every other DILE_CRYPTO symbol remains in the unchanged stock library. */
#include "../model-key.h"

int DILE_CRYPTO_ReadMskToSecureStore(unsigned char *buf, unsigned int *len)
{
    return emulator_read_model_key(buf, len);
}
