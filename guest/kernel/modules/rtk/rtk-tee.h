/* SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors */
/* SPDX-License-Identifier: GPL-2.0-only */
/* rtk.ko's TEE (tee.c: /dev/tee0, OP-TEE's user ABI) and its stand-in TAs (tee-ta.c). */
#ifndef RTK_TEE_H
#define RTK_TEE_H

#include <linux/types.h>

/* GlobalPlatform's TEE Client API values */
#define TEEC_SUCCESS              0x00000000u
#define TEEC_ERROR_GENERIC        0xFFFF0000u
#define TEEC_ERROR_BAD_PARAMETERS 0xFFFF0006u
#define TEEC_ERROR_ITEM_NOT_FOUND 0xFFFF0008u
#define TEEC_ERROR_NOT_SUPPORTED  0xFFFF000Au
#define TEEC_ERROR_OUT_OF_MEMORY  0xFFFF000Cu
#define TEEC_ERROR_SHORT_BUFFER   0xFFFF0010u
#define TEEC_ORIGIN_TEE           3
#define TEEC_ORIGIN_TRUSTED_APP   4

/* a parameter as a TA sees it: type is TEE_IOCTL_PARAM_ATTR_TYPE_*; a value's a and b; a memref's
 * buffer (in the client's shared memory) and size, which an output memref sets to what it wrote
 * (or, with TEEC_ERROR_SHORT_BUFFER, to what it needs) */
struct rtk_ta_param {
    u32 type;
    u32 a, b;
    void *buf;
    size_t size;
};

struct rtk_ta {
    const char *name;
    u8 uuid[16];
    /* called under tee.c's lock; *state is the session's */
    u32 (*open)(void **state, struct rtk_ta_param *p);
    u32 (*invoke)(void *state, u32 cmd, struct rtk_ta_param *p);
    void (*close)(void *state);
};

struct proc_dir_entry;
int rtk_ta_setup(struct proc_dir_entry *dir);
const struct rtk_ta *rtk_ta_find(const u8 *uuid);
void rtk_ta_unknown(const u8 *uuid);

int rtk_tee_add(struct proc_dir_entry *dir);
void rtk_tee_remove(void);

#endif
