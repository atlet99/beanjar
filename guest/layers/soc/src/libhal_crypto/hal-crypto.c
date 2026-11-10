// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libhal_crypto.so.0 for the emulator: the model secret read from the secure store.
 *
 * On the TV, HAL_CRYPTO_ReadMskToSecureStore(buf, &len) reads the "SDP_MODEL" item of the
 * TrustZone secure store through the security engine (/dev/lg/se0, libkadaptor's
 * KADP_SE_ReadSecureStoreDataLength and a decrypt with the SE's key). The item is the model secret
 * key (MSK): a base64 AES-256 key, 44 characters, the same on every set of a model.
 * libsecretagent's SM_SA_GetNDUID encrypts the MAC with it and hashes the result into the NDUID,
 * the device's unique id (nyx DeviceInfo nduid, com.palm.systemservice/deviceInfo, the devmode
 * passphrase, sdx, securitymanager).
 *
 * The emulator has no security engine, so every read failed, SM_SA_GetNDUID failed ("[NDUID]
 * Failed to get ModelSecret"), and the nyx module fell back to a new random NDUID on every open:
 * each process, and each nyx-cmd call, saw a different id. This library answers that one item with
 * the emulator's own synthetic secret by default, provisioned by the host helper: base64 of
 * SHA-256("webos-hypervisor emulator SDP_MODEL secret for 42LB650V"), a string fixed once (the
 * model it names was the board's then, and is not a WT1H model). It is no model's MSK, so LG's SDP
 * rejects it whatever the VM's model name is; its bytes stay as they are, since every keyless VM's
 * NDUID and devmode passphrase follow from them. The NDUID is then derived by
 * LG's own code from the board MAC (board.cmdline ethaddr=), stable across processes and boots.
 * A private provisioned model item replaces it. The host compiles sestore.toml and legacy
 * model-secret into a bundle; initramfs loads it into lgemu before webOS starts. This wrapper
 * reads only /dev/lg/beanjar-sestore, including inside jails. See docs/user/sestore.md;
 * no key value is compiled into this library.
 *
 * It takes the stock library's place under its soname and NEEDs the stock file by its full name
 * (libhal_crypto.so.0.1.0), so every other HAL_CRYPTO_* symbol still resolves to LG's code (and
 * still fails without the security engine, as before). See
 * guest/layers/soc/src/libhal_crypto/meson.build.
 */
#include "../model-key.h"

/* Same contract as the stock function: -1 for NULL arguments or a buffer shorter than the item;
 * otherwise the item (not NUL-terminated), its length in *len, and 0. */
int HAL_CRYPTO_ReadMskToSecureStore(unsigned char *buf, unsigned int *len)
{
    return emulator_read_model_key(buf, len);
}
