// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/compat.h>
#include <linux/devmem_window.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/jhash.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spi/spidev.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/timekeeping.h>
#include "lgemu-drv.h"
#include "lgemu-i2c.h"

#include "lg115x.h"

/* ---- the log manager: /dev/lg/logm ---- */

#define LOGM_MAJOR        1810
#define LOGM_IOC_MAGIC    'a'
#define LOGM_OBJS         200  /* LOGM_LOGOBJ_MAX */
#define LOGM_NAME         32   /* LOGM_MODULE_NAME_MAX */
#define LOGM_LEVELS       7    /* log_level_cnt[LX_LOGM_LEVEL_TRACE + 2] */
#define LOGM_DEFAULT_MASK 0x3  /* LOGM_FORCE_ERR_WARN: ERROR and WARNING */
#define LOGM_LINE_MAX     4096 /* longer user lines are cut */
/* logm_kapi.h's numbers (MASK_CTRL's size is the generation's struct, 16 or 28 bytes) */
#define LOGM_NR_MASK_CTRL    0
#define LOGM_IOR_OBJMAP_SIZE _IOR(LOGM_IOC_MAGIC, 1, u32)
#define LOGM_IOWR_REG_OBJ    _IOWR(LOGM_IOC_MAGIC, 2, struct logm_obj_reg32)
#define LOGM_IOW_PATH_CTRL   _IOW(LOGM_IOC_MAGIC, 3, u32)
#define LOGM_IOW_USER_WRITE  _IOW(LOGM_IOC_MAGIC, 4, struct logm_user_log32)

static unsigned int logm_obj;
module_param(logm_obj, uint, 0444);
MODULE_PARM_DESC(logm_obj,
                 "the size of LX_LOGM_LOGOBJ_T, the logm object table's stride in the chip's "
                 "kernel (76, 80 or 84); 0: no /dev/lg/logm");

/* struct logm_mask_ctrl from a 32-bit process; the 2014 kdriver's ends after ctrl (16 bytes) */
struct logm_mask_ctrl32 {
    s32 fd;
    u32 mask, bit, ctrl;
    u32 name, name_size, bit_name; /* the bit names: user pointers, not kept */
};

struct logm_obj_reg32 {
    char name[LOGM_NAME];
    s32 fd;
    u32 ctrl; /* 0 register, 1 deregister */
};
static_assert(sizeof(struct logm_obj_reg32) == 40);

struct logm_user_log32 {
    s32 fd;
    u32 type, level; /* type 1: hidden, the buffer only */
    u32 data;        /* the formatted line, a user pointer */
    s32 size;
};
static_assert(sizeof(struct logm_user_log32) == 20);

/* LX_LOGM_LOGOBJ_T's words after the name: mask, then user_mask (80 and 84), hidden_mask (84),
 * level, ref_cnt, has_proc and the per-level counts. KADP reads the table through its mapping
 * (name, mask, user_mask, ref_cnt) and writes a line only for a level its mask enables. */
struct logm_layout {
    int user, hidden, ref, count; /* word offsets after the name; -1: not in this layout */
};

static void *logm_map;       /* LOGM_OBJS objects of logm_obj bytes, mapped read-only */
static size_t logm_map_size; /* the mapping: whole pages, a power of two of them */
static struct logm_layout logm_lay;
static DEFINE_SPINLOCK(logm_lock);
static bool logm_path = true, logm_short; /* g_logm_path_ctrl ("force printk", stock on) */

static u32 *logm_word(int fd, int word)
{
    return (u32 *)((u8 *)logm_map + fd * logm_obj + LOGM_NAME) + word;
}

static char *logm_name(int fd)
{
    return (char *)logm_map + fd * logm_obj;
}

static bool logm_name_ok(const char *name)
{
    for (int i = 0; i < LOGM_NAME && name[i]; i++)
        if (!isalnum(name[i]) && name[i] != '_' && name[i] != '-')
            return false;
    return name[0];
}

/* LOGM_ObjRegister: the object's index, a new one with ERROR and WARNING enabled */
static int logm_register(const char *name)
{
    int i;

    for (i = 0; i < LOGM_OBJS; i++) {
        char *n = logm_name(i);

        if (!strncmp(n, name, LOGM_NAME - 1)) {
            (*logm_word(i, logm_lay.ref))++;
            return i;
        }
        if (!n[0]) {
            if (!logm_name_ok(name))
                return -EINVAL;
            strscpy(n, name, LOGM_NAME);
            *logm_word(i, 0) = LOGM_DEFAULT_MASK;
            *logm_word(i, logm_lay.ref) = 1;
            return i;
        }
    }
    return -ENOSPC;
}

static long logm_mask_ctrl(void __user *uarg, unsigned int size)
{
    struct logm_mask_ctrl32 m = {0};
    int word;
    u32 *w, bit;
    long ret = 0;

    if (size != 16 && size != sizeof(m))
        return -ENOTTY;
    if (copy_from_user(&m, uarg, size))
        return -EFAULT;
    /* the mask (0x0-), the user mask (0x1-: a bit set disables) or the hidden mask (0x2-) */
    word = m.ctrl < 0x10 ? 0 : m.ctrl < 0x20 ? logm_lay.user : m.ctrl < 0x30 ? logm_lay.hidden : -1;
    if (word < 0 || ((m.ctrl & 0xf) != 2 && (m.fd < 0 || m.fd >= LOGM_OBJS)))
        return -EINVAL;
    bit = m.bit < 32 ? BIT(m.bit) : 0;
    spin_lock(&logm_lock);
    w = (m.ctrl & 0xf) == 2 ? NULL : logm_word(m.fd, word);
    switch (m.ctrl) {
    case 0x00:
        *w = m.mask | LOGM_DEFAULT_MASK;
        break; /* errors and warnings stay on */
    case 0x10:
    case 0x20:
        *w = m.mask;
        break;
    case 0x01:
    case 0x11:
    case 0x21:
        m.mask = *w;
        break;
    case 0x02:
    case 0x12:
    case 0x22: /* every registered object */
        for (int i = 0; i < LOGM_OBJS && logm_name(i)[0]; i++)
            *logm_word(i, word) = m.mask;
        break;
    case 0x03:
    case 0x14:
    case 0x23:
        *w |= bit;
        break;
    case 0x13:
    case 0x24:
        *w &= ~bit;
        break;
    case 0x04:
        *w = (*w & ~bit) | LOGM_DEFAULT_MASK;
        break;
    case 0x05:
    case 0x06:
        break; /* bit names: not kept */
    case 0x07:
        *w |= m.mask;
        break;
    default:
        ret = -EINVAL;
    }
    if (w && logm_lay.hidden >= 0) /* errors and warnings never hidden */
        *logm_word(m.fd, logm_lay.hidden) &= ~LOGM_DEFAULT_MASK;
    spin_unlock(&logm_lock);
    if (!ret && (m.ctrl & 0xf) == 1 && copy_to_user(uarg, &m, size))
        ret = -EFAULT;
    return ret;
}

static long logm_reg_obj(void __user *uarg)
{
    struct logm_obj_reg32 r;
    long ret = 0;

    if (copy_from_user(&r, uarg, sizeof(r)))
        return -EFAULT;
    r.name[LOGM_NAME - 1] = 0;
    spin_lock(&logm_lock);
    if (r.ctrl == 0) {
        r.fd = logm_register(r.name);
        ret = r.fd < 0 ? r.fd : 0;
    } else {
        ret = -ENOENT;
        for (int i = 0; i < LOGM_OBJS && logm_name(i)[0]; i++)
            if (!strncmp(logm_name(i), r.name, LOGM_NAME - 1)) {
                (*logm_word(i, logm_lay.ref))--;
                ret = 0;
                break;
            }
    }
    spin_unlock(&logm_lock);
    if (ret)
        return ret;
    return copy_to_user(uarg, &r, sizeof(r)) ? -EFAULT : 0;
}

static const char *logm_level_name(u32 level, char *buf)
{
    static const char *const names[] = {"ERR", "WAR", "NOT", "INF", "DEB", "TRA"};

    if (level < ARRAY_SIZE(names))
        return names[level];
    snprintf(buf, 4, "B%02u", level % 100);
    return buf;
}

/* The one deviation from the stock: a line an object has written recently (one of its last
 * LOGM_RECENT distinct lines) is not printed again, only counted. A set has every device its
 * KADP polls; here a poll of a missing one (W17H's HAL_VPQ_GetNoiseLevel with KADP_PE_GetLevelInfo
 * on /dev/lg/pe0, some 33 times a second; WT1H's BE thread and pin muxes) would print the same
 * few errors for ever. A line's count is printed when it leaves the object's recent lines and
 * every LOGM_REPEAT_REPORT repeats. */
#define LOGM_RECENT        16
#define LOGM_REPEAT_REPORT 10000
struct logm_recent {
    u32 hash, repeats;
    u8 level, used; /* used: 0 empty, else the age rank (1 newest) */
};
static struct logm_recent logm_recent[LOGM_OBJS][LOGM_RECENT];

static void logm_printk(const char *name, u32 level, const char *line)
{
    char lv[4];

    if (logm_short)
        printk(KERN_INFO "%-8.8s %-3.3s %s\n", name, logm_level_name(level, lv), line);
    else
        printk(KERN_INFO "%-8.8s %-3.3s (%05d:%05d) %s\n", name, logm_level_name(level, lv),
               task_tgid_vnr(current), task_pid_vnr(current), line);
}

/* Under logm_lock: whether the line (hash, level) of object fd is a recent one (then counted);
 * a count to report goes to *report (its level to *report_level), 0 when none. */
static bool logm_seen(int fd, u32 hash, u32 level, u32 *report, u32 *report_level)
{
    struct logm_recent *r = logm_recent[fd], *hit = NULL, *old = &r[0];
    bool seen;
    u8 rank;

    *report = 0;
    for (int i = 0; i < LOGM_RECENT; i++) {
        if (r[i].used && r[i].hash == hash && r[i].level == level)
            hit = &r[i];
        if (!r[i].used || (old->used && r[i].used > old->used))
            old = &r[i]; /* an empty slot, else the oldest */
    }
    seen = hit;
    if (hit) {
        if (++hit->repeats == LOGM_REPEAT_REPORT) {
            *report = hit->repeats;
            *report_level = level;
            hit->repeats = 0;
        }
    } else {
        if (old->used && old->repeats) {
            *report = old->repeats;
            *report_level = old->level;
        }
        hit = old;
        *hit = (struct logm_recent){.hash = hash, .level = level};
    }
    rank = hit->used; /* the hit becomes the newest; those newer age by one */
    for (int i = 0; i < LOGM_RECENT; i++)
        if (r[i].used && (!rank || r[i].used < rank))
            r[i].used++;
    hit->used = 1;
    return seen;
}

/* LOGM_PutLine for a user line: printk with the object's name and level when the path is on and
 * the level is not hidden, as the stock driver (which keeps no other copy then). The caller's
 * mask decided whether to write it. */
static long logm_user_write(void __user *uarg)
{
    struct logm_user_log32 u;
    char name[LOGM_NAME], *data;
    u32 hash, repeats = 0, report_level = 0;
    bool hidden, repeat = false;
    size_t n;

    if (copy_from_user(&u, uarg, sizeof(u)))
        return -EFAULT;
    if (u.fd < 0 || u.fd >= LOGM_OBJS || u.size <= 0)
        return -EINVAL;
    n = min_t(size_t, u.size, LOGM_LINE_MAX);
    data = memdup_user_nul(compat_ptr(u.data), n);
    if (IS_ERR(data))
        return PTR_ERR(data);
    while (n && (data[n - 1] == '\n' || !data[n - 1]))
        data[--n] = 0;
    hash = jhash(data, n, u.level);
    spin_lock(&logm_lock);
    memcpy(name, logm_name(u.fd), LOGM_NAME);
    hidden = u.type == 1 || (logm_lay.hidden >= 0 && u.level < 32 &&
                             *logm_word(u.fd, logm_lay.hidden) & BIT(u.level));
    (*logm_word(u.fd, logm_lay.count + min_t(u32, u.level, LOGM_LEVELS - 1)))++;
    if (logm_path && !hidden)
        repeat = logm_seen(u.fd, hash, u.level, &repeats, &report_level);
    spin_unlock(&logm_lock);
    name[LOGM_NAME - 1] = 0;
    if (repeats) {
        char text[64];

        snprintf(text, sizeof(text), "(a line repeated %u more times)", repeats);
        logm_printk(name, report_level, text);
    }
    if (logm_path && !hidden && !repeat)
        logm_printk(name, u.level, data);
    kfree(data);
    return 0;
}

static long logm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    void __user *uarg = compat_ptr(arg);
    u32 v;

    if (_IOC_TYPE(cmd) == LOGM_IOC_MAGIC && _IOC_NR(cmd) == LOGM_NR_MASK_CTRL &&
        _IOC_DIR(cmd) == (_IOC_READ | _IOC_WRITE))
        return logm_mask_ctrl(uarg, _IOC_SIZE(cmd));
    switch (cmd) {
    case LOGM_IOR_OBJMAP_SIZE:
        return put_user((u32)logm_map_size, (u32 __user *)uarg);
    case LOGM_IOWR_REG_OBJ:
        return logm_reg_obj(uarg);
    case LOGM_IOW_PATH_CTRL:
        if (get_user(v, (u32 __user *)uarg))
            return -EFAULT;
        if (v <= 1)
            logm_path = v;
        else if (v <= 3)
            logm_short = v == 3;
        else if (v > 5) /* 4, 5: colour, not kept */
            return -EINVAL;
        return 0;
    case LOGM_IOW_USER_WRITE:
        return logm_user_write(uarg);
    }
    return -ENOTTY;
}

static int logm_open(struct inode *inode, struct file *file)
{
    return 0;
}

/* a log daemon (it opens with O_TRUNC) leaving: force printk again, as the stock */
static int logm_release(struct inode *inode, struct file *file)
{
    if (file->f_flags & O_TRUNC)
        logm_path = true;
    return 0;
}

/* The ring of hidden and unprinted lines is not kept: a reader waits for ever, as on a set
 * whose path is on (the stock driver drops each printed line from its ring). */
static ssize_t logm_read(struct file *file, char __user *buf, size_t n, loff_t *pos)
{
    DECLARE_WAIT_QUEUE_HEAD_ONSTACK(never);

    if (file->f_flags & O_NONBLOCK)
        return -EAGAIN;
    return wait_event_interruptible(never, false);
}

static __poll_t logm_poll(struct file *file, struct poll_table_struct *pt)
{
    return 0;
}

/* the object table, read-only (KADP maps it PROT_READ), its whole size only */
static int logm_mmap(struct file *file, struct vm_area_struct *vma)
{
    if (vma->vm_end - vma->vm_start != logm_map_size || vma->vm_pgoff || (vma->vm_flags & VM_WRITE))
        return -EINVAL;
    vm_flags_clear(vma, VM_MAYWRITE);
    return remap_vmalloc_range(vma, logm_map, 0);
}

static const struct file_operations logm_fops = {
    .owner = THIS_MODULE,
    .open = logm_open,
    .release = logm_release,
    .read = logm_read,
    .poll = logm_poll,
    .unlocked_ioctl = logm_ioctl,
    .compat_ioctl = logm_ioctl,
    .mmap = logm_mmap,
};

static struct lgemu_cdev logm_cdev;

void lx_logm_remove(void)
{
    if (!logm_cdev.added)
        return;
    lgemu_cdev_del(&logm_cdev);
    vfree(logm_map);
}

int lx_logm_add(void)
{
    int err;

    if (!logm_obj)
        return 0;
    switch (logm_obj) {
    case 76:
        logm_lay = (struct logm_layout){-1, -1, 2, 4};
        break; /* the 2014 kdriver's */
    case 80:
        logm_lay = (struct logm_layout){1, -1, 3, 5};
        break; /* user_mask */
    case 84:
        logm_lay = (struct logm_layout){1, 2, 4, 6};
        break; /* and hidden_mask */
    default:
        pr_err("logm: logm_obj is 76, 80 or 84\n");
        return -EINVAL;
    }
    logm_map_size = PAGE_SIZE << get_order(logm_obj * LOGM_OBJS);
    logm_map = vmalloc_user(logm_map_size);
    if (!logm_map)
        return -ENOMEM;
    err = lgemu_cdev_add(&logm_cdev, LOGM_MAJOR, 0, 1, "logm", &logm_fops);
    if (err) {
        vfree(logm_map);
        return err;
    }
    pr_info("logm: logm, %u objects of %u bytes, user lines to printk\n", LOGM_OBJS, logm_obj);
    return 0;
}
