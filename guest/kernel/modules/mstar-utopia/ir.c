// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* LG's direct MStar IR read ABI, from W18R's mdrv_ir_io.c / mdrv_ir_io.h.
 * W21A MAdp_GetKeyEventWithTimeout uses the same 24-byte record and expects
 * read() to return zero on a received frame, -ENOTTY on timeout. The host's
 * remote edges use lgemu's existing receiver queue. */
#include "private.h"

#define IR_CMD(n) _IOW('u', n, int)
static bool enabled = true;
static int master_pid;
struct ir_read_arg {
    u32 timeout;
    u8 key_type, key_value, type, raw[4], tvlink[10], padding[3];
};
static_assert(sizeof(struct ir_read_arg) == 24);

static ssize_t ir_read(struct file *file, char __user *buf, size_t count, loff_t *off)
{
    struct ir_read_arg a;
    unsigned long deadline;
    u8 command = 0xc0, event[3];
    struct i2c_msg msgs[] = {
        {.len = 1, .buf = &command},
        {.flags = I2C_M_RD, .len = sizeof(event), .buf = event},
    };

    if (count < sizeof(a))
        return -EINVAL;
    if (copy_from_user(&a, buf, sizeof(a)))
        return -EFAULT;
    deadline = jiffies + min_t(unsigned long, a.timeout, MAX_JIFFY_OFFSET);
    pr_info_once("IR read: native record %zu bytes, timeout %u ticks\n", count, a.timeout);
    for (;;) {
        if (READ_ONCE(enabled)) {
            int ret = lgemu_i2c_xfer(LGEMU_I2C_MICOM, msgs, ARRAY_SIZE(msgs));

            if (ret < 0)
                return ret;
            /* Micom event 2 is a single NEC frame, 3 is its repeat. Release
             * edges stop repeats; the native reader infers release on timeout. */
            if (event[0] == 2 || event[0] == 3) {
                a.timeout = time_before(jiffies, deadline) ? deadline - jiffies : 0;
                a.type = event[0] == 2 ? 1 : 2;
                a.raw[0] = 0x04;
                a.raw[1] = 0xfb;
                a.raw[2] = event[1];
                a.raw[3] = event[2];
                return copy_to_user(buf, &a, sizeof(a)) ? -EFAULT : 0;
            }
        }
        if (signal_pending(current))
            return -ERESTARTSYS;
        if (time_after_eq(jiffies, deadline))
            return -ENOTTY;
        schedule_timeout_interruptible(
            min_t(unsigned long, deadline - jiffies, max_t(unsigned long, 1, msecs_to_jiffies(5))));
    }
}

static long ir_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    void __user *uarg = (void __user *)arg;
    u32 delay[2];
    u8 on;

    ioctl_count("ir", cmd);
    switch (cmd) {
    case _IO('u', 0):
        return 0;
    case IR_CMD(1):
        if (copy_from_user(delay, uarg, sizeof(delay)))
            return -EFAULT;
        lgemu_ir_set_delay(delay[0], delay[1]);
        return 0;
    case IR_CMD(6):
        if (get_user(on, (u8 __user *)uarg))
            return -EFAULT;
        WRITE_ONCE(enabled, !!on);
        return 0;
    case IR_CMD(9):
        return get_user(master_pid, (int __user *)uarg);
    case IR_CMD(10):
        return put_user(master_pid, (int __user *)uarg);
    default:
        return unhandled("ir", cmd, -ENOTTY);
    }
}

const struct file_operations ir_fops = {
    .owner = THIS_MODULE,
    .open = plain_open,
    .read = ir_read,
    .unlocked_ioctl = ir_ioctl,
    .compat_ioctl = ir_ioctl,
};
