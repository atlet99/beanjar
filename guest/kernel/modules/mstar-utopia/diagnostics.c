// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
#include "private.h"

/* Every ioctl of every device here is counted by (device, command), and reading
 * /proc/mstar-utopia/ioctls lists them, so a boot shows which ones a caller repeats and how often
 * (a cat 1 s apart gives the rate). One this does not know is logged the first time its (device,
 * command) is seen, with the answer the caller gets; the count says how often it came after that.
 */
static int ioctls_show(char *buf, size_t size, const struct lgemu_count *e)
{
    return scnprintf(buf, size, "%s %#x %lu%s\n", e->what, e->a, e->count,
                     e->marked ? " not-emulated" : "");
}

DEFINE_LGEMU_COUNTS(ioctls, 128, 64, ioctls_show);

/* Debugging: trace_cmd logs who issues one command (at most once a second): the thread, and its
 * AArch32 pc, lr and the word at sp + 8 (for a caller through glibc's ioctl from a push {r3, lr}
 * wrapper such as MsOS_ReadMemory, the wrapper's own return address). trace_cmd=<cmd> is an
 * ioctl command of any device here (the ioctls table), trace_cmd=<module>:<cmd> a kernel-mode
 * Utopia module's command (/proc/utopia's table, e.g. 42:5, GPIO's pad read); 0 or empty: none. */
static struct lgemu_trace ioctls_trace, utopia_trace;
static char trace_cmd[24];

static int trace_cmd_set(const char *val, const struct kernel_param *kp)
{
    bool pair = strchr(val, ':');
    struct lgemu_trace *t;
    u32 w[2] = {0, 0};
    int err = 0;

    if (*val && *val != '\n')
        err = lgemu_parse_u32s(val, 0, w, pair ? 2 : 1);
    if (err)
        return err;
    WRITE_ONCE(ioctls_trace.on, false);
    WRITE_ONCE(utopia_trace.on, false);
    smp_wmb();
    t = pair ? &utopia_trace : &ioctls_trace;
    t->a = w[0];
    t->b = w[1];
    smp_wmb();
    WRITE_ONCE(t->on, pair || w[0]);
    strscpy(trace_cmd, val, sizeof(trace_cmd));
    strim(trace_cmd);
    return 0;
}

static int trace_cmd_get(char *buf, const struct kernel_param *kp)
{
    return sysfs_emit(buf, "%s\n", trace_cmd);
}

static const struct kernel_param_ops trace_cmd_ops = {.set = trace_cmd_set, .get = trace_cmd_get};
module_param_cb(trace_cmd, &trace_cmd_ops, NULL, 0644);
MODULE_PARM_DESC(trace_cmd, "<ioctl> or <utopia module>:<command> whose callers are logged "
                            "(debugging; 0: none)");

void ioctl_count(const char *dev, unsigned int cmd)
{
    lgemu_count(&ioctls, dev, cmd, 0);
}

long unhandled(const char *dev, unsigned int cmd, long ret)
{
    if (lm14 && !ret)
        ret = -ENOTTY;
    return lgemu_ioctl_unhandled(&ioctls, dev, cmd, ret);
}

void diagnostics_init(void)
{
    ioctls.trace = &ioctls_trace;
}
struct lgemu_trace *diagnostics_utopia_trace(void)
{
    return &utopia_trace;
}
bool diagnostics_proc_create(struct proc_dir_entry *dir)
{
    return proc_create_data("ioctls", 0444, dir, &lgemu_counts_proc_ops, &ioctls) != NULL;
}
