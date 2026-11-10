// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* O22 UCOM command transport, backed by lgemu's shared virtual micom.
 * LG's webOS22 O2.0 include/ucom_kapi.h and chip/o22/ucom define the
 * 40-byte request and 24-byte payload limit. The command is separate from
 * the payload; the user's pointer is the second pointer union (offset 32).
 * No vendor mailbox, firmware image or interrupt delivery is simulated. */
#include <linux/compat.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include "lgemu-drv.h"
#include "lgemu-i2c.h"
#include "lg115x.h"

static bool ucom;
module_param(ucom, bool, 0444);
MODULE_PARM_DESC(ucom, "O22 UCOM transport at /dev/lg/ucom0, using the shared virtual micom");

struct lx_ucom_request {
    u32 selector, nvrw, nvaddr, nvdata, size;
    u8 valid, opcode, command, reserved;
    __aligned_u64 buffer;
    __aligned_u64 user_buffer;
};
static_assert(sizeof(struct lx_ucom_request) == 40);
static_assert(offsetof(struct lx_ucom_request, command) == 22);
static_assert(offsetof(struct lx_ucom_request, user_buffer) == 32);

#define UCOM_INIT     _IO('u', 0)
#define UCOM_WRITE    _IOW('u', 10, struct lx_ucom_request)
#define UCOM_READ     _IOWR('u', 11, struct lx_ucom_request)
#define UCOM_MAX_DATA 24
#define UCOM_MAJOR    1144 /* core/base/base_device.h: BASE_DEV_MAJOR + 44 */

static struct lgemu_cdev ucom_cdev;
static DEFINE_MUTEX(ucom_lock);

static long lx_ucom_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct lx_ucom_request request;
    void __user *data;
    long ret;

    if (cmd == UCOM_INIT)
        return 0; /* The virtual micom is ready when its transport is registered. */
    if (cmd != UCOM_READ && cmd != UCOM_WRITE)
        return -ENOTTY;
    if (copy_from_user(&request, (void __user *)arg, sizeof(request)))
        return -EFAULT;
    if (request.size > UCOM_MAX_DATA)
        return -EINVAL;
    data = in_compat_syscall() ? compat_ptr((u32)request.user_buffer)
                               : u64_to_user_ptr(request.user_buffer);
    mutex_lock(&ucom_lock);
    ret = lgemu_i2c_user_xfer(LGEMU_I2C_MICOM, 0, &request.command, 1, data, request.size,
                              cmd == UCOM_READ);
    mutex_unlock(&ucom_lock);
    return ret < 0 ? ret : 0;
}

static const struct file_operations lx_ucom_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = lx_ucom_ioctl,
    .compat_ioctl = lx_ucom_ioctl,
    .llseek = noop_llseek,
};

int lx_ucom_add(void)
{
    struct device *dev;
    int ret;

    if (!ucom)
        return 0;
    ret = lgemu_cdev_add(&ucom_cdev, UCOM_MAJOR, 0, 1, "ucom", &lx_ucom_fops);
    if (ret)
        return ret;
    dev = device_create(&lx_class, NULL, ucom_cdev.dev, NULL, "ucom0");
    if (IS_ERR(dev)) {
        lgemu_cdev_del(&ucom_cdev);
        return PTR_ERR(dev);
    }
    return 0;
}

void lx_ucom_remove(void)
{
    if (ucom_cdev.added) {
        device_destroy(&lx_class, ucom_cdev.dev);
        lgemu_cdev_del(&ucom_cdev);
    }
}
