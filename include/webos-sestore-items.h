// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef WEBOS_SESTORE_ITEMS_H
#define WEBOS_SESTORE_ITEMS_H
/* The names of LG's secure store ("sestore") items that the emulator knows, as SESTORE_ReadData(id,
 * ...) takes them: 8 hex digits, the item's id in the store. Item ids only, never their contents.
 * The store is read by libsestore (guest/layers/soc/src/libsestore/sestore.c answers the ones the
 * VM provides). */

/* SDP model secret key (SDP_MODEL, the model key). nyxCrypto asks for it by the TV's broadcast
 * region, a region-suffixed id first, then the plain one. */
#define SESTORE_ITEM_SDP_MODEL         "1D492999"
#define SESTORE_ITEM_SDP_MODEL_REGIONS "adr" /* the region letters that follow "_" */
#define SESTORE_ITEM_SDP_MODEL_A       SESTORE_ITEM_SDP_MODEL "_a"
#define SESTORE_ITEM_SDP_MODEL_D       SESTORE_ITEM_SDP_MODEL "_d"
#define SESTORE_ITEM_SDP_MODEL_R       SESTORE_ITEM_SDP_MODEL "_r"

/* Second screen gateway (SSG, SSAP) TLS private key, read by securitymanager's getSSGSSL. */
#define SESTORE_ITEM_SSG "77D1DD90"

/* YouTube (Cobalt) device authentication key, read by DILE_CRYPTO_YOUTUBE_ReadPrivKey. */
#define SESTORE_ITEM_YOUTUBE "C1FFB834"
#endif
