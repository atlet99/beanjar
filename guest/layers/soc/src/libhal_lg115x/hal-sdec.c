// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libhal_lg115x for the emulator: the section decoder (SDEC, the TS demultiplexer) of a board
 * without LG's SDEC driver.
 *
 * tv_sysonoff_init brings it up once (API_DDM_INIT_OEMChip -> API_SDEC_InitializeModule ->
 * DIL_SDEC_InitializeModule -> HAL_SDEC_Initialize(platform, country)). The stock HAL opens the
 * device through KADP (KADP_SDEC_Open, /dev/lg/sdec0) and reads its chip configuration
 * (KADP_SDEC_GetChipCfg). Without the driver:
 *   - H13's KADP keeps its SDEC state in a static object: the open fails, GetChipCfg's ioctl
 *     fails on fd -1, and HAL_SDEC_Initialize returns -1. tvservice makes no other SDEC call
 *     then;
 *   - M14's KADP (W15M's libkadaptor.so.2) allocates that object in KADP_SDEC_Open, which returns
 *     NULL when the device cannot be opened, and GetChipCfg and every other KADP_SDEC_* function
 *     read through the pointer without a check: tvservice died with SEGV at 0x4 in
 *     KADP_SDEC_GetChipCfg, 2.4 s into its start.
 * So when the device cannot be opened (the node is there, from /etc/device_table, but no driver
 * is behind it) this answers as H13's HAL does, -1, before the stock code runs; with a driver the
 * stock HAL runs. Every other HAL_SDEC_* function stays LG's. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "tvservice-hal.h"

#define SDEC_DEV "/dev/lg/sdec0" /* KADP_SDEC_Open's device */

typedef int sdec_init_fn(unsigned, unsigned);

int HAL_SDEC_Initialize(unsigned platform, unsigned country)
{
    static sdec_init_fn *stock_init;
    int fd = open(SDEC_DEV, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        hal_log("sdec: %s: %s: HAL_SDEC_Initialize fails (-1), as without LG's driver", SDEC_DEV,
                strerror(errno));
        return -1;
    }
    close(fd);
    if (!stock_init && !(stock_init = (sdec_init_fn *)dlsym(RTLD_NEXT, "HAL_SDEC_Initialize")))
        return -1;
    return stock_init(platform, country);
}
