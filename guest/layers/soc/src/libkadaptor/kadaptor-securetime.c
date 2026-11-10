// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libkadaptor.so.2 for the emulator on W22O queue: the secure clock's PlayReady TA call.
 *
 * After LG's login answers 200, sdx sets the time it got from the server with libsecuretime's
 * st_initializeTime: /dev/shm/ntptime, then nyx's DrmImpl::setSecureClock, libdrmddi's
 * HAL_DRM_SetSecureClock and KADP's LGSOC_DRM_SetSecureClock. That one keeps the time against
 * CLOCK_MONOTONIC for LGSOC_DRM_GetSecureClock, then hands it to PlayReady's trusted application
 * with playready_ca_interface_set_drm_time. The VM has no TEE: prepare_session_for_playready fails,
 * so the call failed, LGSOC_DRM_SetSecureClock returned -1, st_initializeTime -1, and sdx kept
 * isDeviceAuthenticated false (retrying in 600 s). eula-service only starts once sdx is
 * authenticated, so the User Agreements had no terms to show.
 *
 * LGSOC_DRM_SetSecureClock takes TEEC_ERROR_NOT_SUPPORTED from the TA call as success, a TEE
 * without PlayReady's clock. That is what this VM is, so this library answers the call with it;
 * every other symbol is the stock library's, which it NEEDs by its full name
 * (guest/layers/soc/src/libkadaptor/meson.build). KADP calls the function through its PLT, so
 * this definition is the one it reaches.
 */
#include <stdint.h>

#define TEEC_ERROR_NOT_SUPPORTED 0xFFFF000Au

/* The stock function takes the time in seconds (LGSOC_DRM_SetSecureClock passes the low word of
 * its 64-bit argument) and returns a TEEC_Result. */
uint32_t playready_ca_interface_set_drm_time(uint32_t seconds)
{
    (void)seconds;
    return TEEC_ERROR_NOT_SUPPORTED;
}
