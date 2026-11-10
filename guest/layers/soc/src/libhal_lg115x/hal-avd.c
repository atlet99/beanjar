// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libhal_lg115x.so.0 for the emulator: the analog video decoder (AVD) of a set with no antenna
 * signal.
 *
 * Live TV on an analog channel (the input of a new NVM: ATV) connects the scaler to the AVD
 * (API_VIDEO_AVD_Open/Connect -> DIL_VFE_AVD_* -> HAL_VFE_AVD_*). Without LG's driver the stock
 * HAL fails those (Initialize -1, Open and Connect -3), and tvservice then never starts watching
 * the AVD's sync, so it never decides that there is no signal: Live TV drew nothing. A healthy
 * decoder with nothing on its input answers the set-up calls OK and reports no sync:
 *   HAL_VFE_AVD_Initialize, _Open, _Connect(src, port), _Disconnect, _Close   OK
 *   HAL_VFE_AVD_DoesSyncExist()          0
 *   HAL_VFE_AVD_GetTimingInfo(timing)    0, with the stock HAL's answer for a CVD without sync
 * The last two must be answered here: the stock ones read the CVD through KADP
 * (KADP_AFE_CVD_Sync_Exist, _Get_Timing_Info, _Get_Lock_States), which fail without LG's driver
 * and write nothing, and the HAL then hands on its own uninitialised stack and returns 0. That
 * "timing" changed at every call and at every boot, sometimes locked: tvservice's AVD task
 * (VIDEO_AVD_PeriodicTask, every 120 ms) saw either no signal, or a good signal that changed at
 * every poll (ARC, PQ and 3D messages and a SettingsService write each time), by the boot.
 *
 * The stock HAL_VFE_AVD_GetTimingInfo builds VFE_AVD_TIMING_INFO_T from the CVD:
 *   vdLock = Sync_Exist; HLock, VLock = lock-state bits 1 and 2; standard from the CVD's colour
 *   standard, VIDEO_AVDEC_MODE_UNKNOWN (9) for a non-standard one with nothing locked;
 *   with no sync on an external input (the source HAL_VFE_AVD_Connect recorded is 1 or 2) all
 *   timing fields 0; otherwise (the tuner, source 0) the CVD's own: hFreq, vFreq, hPorch, vPorch
 *   and the active size from its HFreq, VFreq, HStart, VStart, HSize and VSize (hTotal, vTotal
 *   and the active position always 0).
 * What a CVD without a signal reports is taken from LG's CVD kernel driver in the starfish SDK
 * (webOS 5.0, 2019: lg_kdriver/core/afe/cvd_module.c CVD_Get_Timing_Info; WT1H's driver is not
 * available): standard LX_NON_STANDARD and the free-running vertical rate, 50.0 Hz (500) on a
 * 625-line system (the set is European), nothing else written. So the tuner reports vFreq 500
 * and the rest 0. tvservice keys a timing by its active size (format = w << 12 | h, 0 when vFreq
 * is 0), so both answers are its "no timing": it counts the input bad and stable, and
 * _VIDEO_AVD_DetectingATVNoSignal tells Live TV "Bad-Signal": LG's No Signal screen. (A made-up
 * free-running timing with an active size, 720x576 at 50 Hz, made the task count the input as
 * good: no No Signal screen.) Every other AVD call (VBI, colour system, SCART) stays LG's.
 *
 * Built with HAL_AVD_OVER_STOCK it is MStar's (guest/layers/soc/src/libhal_avd, M2 and M3): only
 * DoesSyncExist and GetTimingInfo are answered, Connect noted on its way to the stock one. */
#include "tvservice-hal.h"
#include <stdint.h>
#include <string.h>
#ifdef HAL_AVD_OVER_STOCK
#include <dlfcn.h> /* RTLD_NEXT: built with _GNU_SOURCE */
#endif

#define HAL_OK             0
#define AVD_SRC_TYPE_ATV   0   /* VFE_AVD_SRC_TYPE_T */
#define AVD_MODE_UNKNOWN   9   /* VIDEO_AVDEC_MODE_UNKNOWN */
#define CVD_FREE_RUN_VFREQ 500 /* 0.1 Hz: the CVD free-running at 50 Hz (625 lines) */

/* VFE_AVD_TIMING_INFO_T, from tvservice's DWARF (44 bytes; the SDK's hal_vfe_avd.h agrees) */
typedef struct {
    uint32_t format;
    uint16_t hFreq, vFreq, hTotal, vTotal, hPorch, vPorch;
    uint16_t active[4]; /* VFE_AVD_RECT_T {w, h, x, y} */
    uint8_t scanType;
    uint32_t vdLock, HLock, VLock;
    uint32_t standard; /* VFE_AVD_AVDECODER_VIDEO_MODE_T */
} avd_timing_t;
_Static_assert(sizeof(avd_timing_t) == 44, "VFE_AVD_TIMING_INFO_T");

static volatile unsigned source = AVD_SRC_TYPE_ATV;

#ifndef HAL_AVD_OVER_STOCK
int HAL_VFE_AVD_Initialize(void)
{
    return HAL_OK;
}
int HAL_VFE_AVD_Open(void)
{
    return HAL_OK;
}
int HAL_VFE_AVD_Close(void)
{
    return HAL_OK;
}
int HAL_VFE_AVD_Connect(unsigned src, unsigned port)
{
    hal_log("avd: connect source %u port %u: no signal", src, port & 0xff);
    source = src;
    return HAL_OK;
}
int HAL_VFE_AVD_Disconnect(unsigned src, unsigned port)
{
    (void)src;
    (void)port;
    return HAL_OK;
}
#else
/* Over a stock HAL that runs the decoder itself (MStar's, libhal_avd): its set-up calls stay LG's,
 * Connect only noted on the way (the source GetTimingInfo answers for). */
int HAL_VFE_AVD_Connect(unsigned src, unsigned port)
{
    static int (*stock)(unsigned, unsigned);
    if (!stock)
        stock = (int (*)(unsigned, unsigned))dlsym(RTLD_NEXT, "HAL_VFE_AVD_Connect");
    hal_log("avd: connect source %u port %u: no signal", src, port & 0xff);
    source = src;
    return stock ? stock(src, port) : HAL_OK;
}
#endif
int HAL_VFE_AVD_DoesSyncExist(void)
{
    return 0;
}
int HAL_VFE_AVD_GetTimingInfo(avd_timing_t *timing)
{
    memset(timing, 0, sizeof *timing);
    if (source == AVD_SRC_TYPE_ATV)
        timing->vFreq = CVD_FREE_RUN_VFREQ;
    timing->standard = AVD_MODE_UNKNOWN;
    return HAL_OK;
}
