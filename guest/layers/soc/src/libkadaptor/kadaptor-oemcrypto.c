// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libkadaptor.so.2 for the emulator on O22 and its successors: Widevine's OEMCrypto_Initialize
 * without a TEE.
 *
 * O22's KADP carries Widevine's OEMCrypto (the obfuscated names of OEMCryptoCENC.h: _oecc01 is
 * OEMCrypto_Initialize), which the firmware's CE CDM (libwidevine_ce_cdm_shared) imports; Cobalt
 * reaches it when it sets up EME. Every entry talks to Widevine's trusted application through
 * KADP's own TEE client, whose TEEC_InitializeContext opens /dev/lg/se0 (teec_prepare_io_resource).
 * The VM has no secure world and no such node, and KADP has no failure path there: on W22O number1
 * (13.30.56), _oecc01's wv_sobj_migration_current calls wvca_open_session, which copies 36 bytes
 * from what wvca_prepare_session returns, and that is NULL once ca_open_session fails
 * (TEEC_InitializeContext: TEEC_ERROR_GENERIC). Cobalt died there with SIGSEGV
 * (wvca_open_session+0x1c). _oecc01 ignores the failures after that too (wv_cenc_open_session,
 * wv_cenc_invoke_command) and returns wv_cenc_mgn_init's answer. Called without a TEE (qemu-arm,
 * each firmware's own libraries), mullet's (04.40.93) and W23O number1's (13.40.76) _oecc01 crash
 * in wvca_open_session as well; ponytail's (33.31.68) and queue's (43.21.89) return 28, a failure.
 *
 * The CE CDM checks OEMCrypto_Initialize: on anything but OEMCrypto_SUCCESS its CryptoSession::Init
 * logs "OEMCrypto_Initialize failed: status = %d" (core/src/crypto_session.cpp:291) and stays
 * uninitialised, and every later crypto session fails "Crypto session is not initialized", so
 * Widevine is unavailable and clear content plays on. So when KADP's TEE client cannot reach a
 * TEE, this library answers OEMCrypto_ERROR_INIT_FAILED (1) before the stock code runs, as the
 * later KADPs fail on their own; with a TEE it is the stock function. Every other symbol is the
 * stock library's, which it NEEDs by its full name (guest/layers/soc/src/libkadaptor/meson.build).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <syslog.h>

#define OEMCRYPTO_ERROR_INIT_FAILED 1u
#define TEEC_SUCCESS                0u

/* KADP's TEEC_Context is one word (the I/O resource teec_prepare_io_resource returns); the
 * spare words cover a later KADP's larger one. TEEC_FinalizeContext keeps no state in it. */
typedef struct {
    void *imp[8];
} teec_context;

uint32_t _oecc01(void)
{
    uint32_t (*init_context)(const char *name, teec_context *context) =
        dlsym(RTLD_NEXT, "TEEC_InitializeContext");
    void (*finalize_context)(teec_context *context) = dlsym(RTLD_NEXT, "TEEC_FinalizeContext");
    uint32_t (*stock)(void) = dlsym(RTLD_NEXT, "_oecc01");
    teec_context context = {{0}};

    if (!init_context || !stock || init_context(NULL, &context) != TEEC_SUCCESS) {
        syslog(LOG_WARNING,
               "libkadaptor (emulator): no TEE, OEMCrypto_Initialize fails (Widevine unavailable)");
        return OEMCRYPTO_ERROR_INIT_FAILED;
    }
    if (finalize_context)
        finalize_context(&context);
    return stock();
}
