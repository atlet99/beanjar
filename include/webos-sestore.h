// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The emulator's provisioned store ABI, shared by arm64 boot helpers, ARM32 readers and lgemu.
 * Every ioctl record has the same layout in both ABIs. Provisioning is once per module load.
 * The bundle is little-endian: a header, then count records each followed by its exact bytes. */
#ifndef WEBOS_SESTORE_H
#define WEBOS_SESTORE_H
#include <linux/ioctl.h>
#include <linux/types.h>

#define WEBOS_SESTORE_DEVICE           "/dev/lg/beanjar-sestore"
#define WEBOS_SESTORE_MAX_NAME         64
#define WEBOS_SESTORE_MAX_VALUE        65536
#define WEBOS_SESTORE_MAX_ITEMS        256
#define WEBOS_SESTORE_MAX_BUNDLE       (8u + WEBOS_SESTORE_MAX_ITEMS * (72u + WEBOS_SESTORE_MAX_VALUE))
#define WEBOS_SESTORE_MISSING_ERROR    0u
#define WEBOS_SESTORE_MISSING_FALLBACK 1u

struct webos_sestore_header {
    __u32 missing;
    __u32 count;
};
struct webos_sestore_record {
    char id[WEBOS_SESTORE_MAX_NAME + 1];
    __u8 reserved[3];
    __u32 bytes;
};
struct webos_sestore_load {
    __aligned_u64 data;
    __u32 bytes;
    __u32 reserved;
};
struct webos_sestore_get {
    __aligned_u64 data; /* NULL is a size query; capacity ignored for a query. */
    __u32 capacity;
    __u32 bytes; /* output length; also returned on EMSGSIZE. */
    char id[WEBOS_SESTORE_MAX_NAME + 1];
    __u8 reserved[7];
};
struct webos_sestore_info {
    __u32 missing;
    __u32 count;
};

#define WEBOS_SESTORE_LOAD _IOW('W', 0x50, struct webos_sestore_load)
#define WEBOS_SESTORE_GET  _IOWR('W', 0x51, struct webos_sestore_get)
#define WEBOS_SESTORE_INFO _IOR('W', 0x52, struct webos_sestore_info)

_Static_assert(sizeof(struct webos_sestore_header) == 8, "store bundle header");
_Static_assert(sizeof(struct webos_sestore_record) == 72, "store bundle record");
_Static_assert(sizeof(struct webos_sestore_load) == 16, "store load ioctl");
_Static_assert(sizeof(struct webos_sestore_get) == 88, "store read ioctl");
_Static_assert(sizeof(struct webos_sestore_info) == 8, "store info ioctl");
#endif
