// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* /dev/lgib, LG's instant-boot device (CONFIG_LG_INSTANT_BOOT, guest/kernel/power/user.c in LG's
 * GPL kernels for W16R to W22H): Quick Start+'s suspend-to-RAM. tvpowerd's
 * PowerStatesMgr::stateSuspendReady opens it, calls LGIB_SUSPEND_TO_RAM (retrying while it fails
 * with EAGAIN) and closes it again after the resume; the call returns when the set wakes.
 *
 * As LG's driver: one open at a time (a second gets EBUSY until the first is closed), the call
 * needs CAP_SYS_ADMIN, and other commands of the 'i' type fail. LG's calls
 * pm_suspend(PM_SUSPEND_MEM); here "mem" is suspend-to-idle (mem_sleep_default=s2idle, QEMU's virt
 * machine has no PSCI SYSTEM_SUSPEND, docs/dev/architecture/suspend.md), so this suspends to idle,
 * what a write of "mem" to /sys/power/state does on this kernel. The guest wakes on the micom's
 * power line (i2c.c). */
#define pr_fmt(fmt) KBUILD_MODNAME ": lgib: " fmt
#include <linux/atomic.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include "lgemu.h"

#define LGIB_IOC_MAGIC      'i'
#define LGIB_SUSPEND_TO_RAM _IO(LGIB_IOC_MAGIC, 1)
#define LGIB_IOC_MAXNR      1

static atomic_t available = ATOMIC_INIT(1);

static int lgib_open(struct inode *inode, struct file *file)
{
    return atomic_add_unless(&available, -1, 0) ? 0 : -EBUSY;
}

static int lgib_release(struct inode *inode, struct file *file)
{
    atomic_inc(&available);
    return 0;
}

static long lgib_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret;

    if (_IOC_TYPE(cmd) != LGIB_IOC_MAGIC || _IOC_NR(cmd) > LGIB_IOC_MAXNR)
        return -ENOTTY;
    if (!capable(CAP_SYS_ADMIN))
        return -EPERM;
    if (cmd != LGIB_SUSPEND_TO_RAM)
        return -EPERM; /* LG's answer to the other numbers (its ret = -1) */
    pr_info("suspend to RAM (s2idle) for %s[%d]\n", current->comm, task_pid_nr(current));
    ret = pm_suspend(PM_SUSPEND_TO_IDLE);
    pr_info("resumed (%d)\n", ret);
    return ret;
}

static const struct file_operations lgib_fops = {
    .owner = THIS_MODULE,
    .open = lgib_open,
    .release = lgib_release,
    .unlocked_ioctl = lgib_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
    .llseek = noop_llseek,
};

static struct miscdevice lgib_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "lgib",
    .fops = &lgib_fops,
};

int lgemu_lgib_init(void)
{
    return misc_register(&lgib_device);
}

void lgemu_lgib_exit(void)
{
    misc_deregister(&lgib_device);
}
