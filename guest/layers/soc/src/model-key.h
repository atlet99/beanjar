// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* HAL and DILE keep their capacity ABI; all identity material comes from the kernel store. */
#ifndef EMULATOR_MODEL_KEY_H
#define EMULATOR_MODEL_KEY_H
#include "sestore-item.h"
#include "../../../../include/webos-sestore-items.h"

static int emulator_read_model_key(unsigned char *buf, unsigned int *len)
{
    unsigned int size = 0;
    if (!buf || !len || *len < 44)
        return -1;
    int result = emulator_sestore_read(SESTORE_ITEM_SDP_MODEL, buf, &size, *len);
    if (result == 1)
        result = emulator_sestore_read("SDP_MODEL", buf, &size, *len);
    if (result || size != 44)
        return -1;
    *len = size;
    return 0;
}
#endif
