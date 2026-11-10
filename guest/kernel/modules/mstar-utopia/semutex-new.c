// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* Named semaphores follow MStar's mdrv_semutex.c and drvMPool.c. W21A's
 * additional RW-lock requests come from its libkadaptor.
 * Separate namespaces: RW-lock IDs start at 255, semaphore IDs at 0x55660000.
 * The kernel objects persist across opens, as the named mutexes do. */
#include "private.h"

#define SLOTS    320
#define NAME_LEN 64
#define RW_BASE  255
#define SEM_BASE 0x55660000
#define CMD(n)   _IOR('S', n, int)

static DEFINE_MUTEX(names_lock);
static struct {
    char name[NAME_LEN];
    struct semaphore sem;
    spinlock_t lock;
    unsigned int taken;
} named_sem[SLOTS];
static struct {
    char name[NAME_LEN];
    spinlock_t lock;
    wait_queue_head_t wait;
    unsigned int readers;
    bool writer;
} named_rw[SLOTS];

static long create_named(void __user *uarg, bool semaphore)
{
    char name[NAME_LEN];
    u32 initial = 0;
    int free = -1;
    long n;

    /* Semaphore argument: initial count, name[64]. RW argument: name[64]. */
    if (semaphore) {
        if (get_user(initial, (u32 __user *)uarg))
            return -EFAULT;
        if (!initial || initial > INT_MAX)
            return -EINVAL;
        uarg = (u8 __user *)uarg + 4;
    }
    n = strncpy_from_user(name, uarg, sizeof(name));
    if (n <= 0)
        return n < 0 ? n : -EINVAL;
    name[NAME_LEN - 1] = 0;
    mutex_lock(&names_lock);
    for (int i = 0; i < SLOTS; i++) {
        char *stored = semaphore ? named_sem[i].name : named_rw[i].name;

        if (!strcmp(stored, name)) {
            mutex_unlock(&names_lock);
            return (semaphore ? SEM_BASE : RW_BASE) + i;
        }
        if (free < 0 && !stored[0])
            free = i;
    }
    if (free >= 0) {
        if (semaphore) {
            sema_init(&named_sem[free].sem, initial);
            named_sem[free].taken = 0;
            strscpy(named_sem[free].name, name, NAME_LEN);
        } else {
            strscpy(named_rw[free].name, name, NAME_LEN);
        }
    }
    mutex_unlock(&names_lock);
    return free < 0 ? -ENOSPC : (semaphore ? SEM_BASE : RW_BASE) + free;
}

static bool rw_try(int i, bool writer)
{
    bool acquired = false;
    unsigned long flags;

    spin_lock_irqsave(&named_rw[i].lock, flags);
    if (!named_rw[i].writer && (!writer || !named_rw[i].readers)) {
        if (writer)
            named_rw[i].writer = true;
        else
            named_rw[i].readers++;
        acquired = true;
    }
    spin_unlock_irqrestore(&named_rw[i].lock, flags);
    return acquired;
}

/* -ENOIOCTLCMD means the old semutex dispatcher should handle the command. */
long semutex_new_ioctl(unsigned int cmd, void __user *uarg)
{
    int id, i, ret;
    u32 wait_ms;
    bool writer;
    unsigned long flags;

    if (cmd == CMD(12))
        return create_named(uarg, true);
    if (cmd == CMD(18))
        return create_named(uarg, false);
    if (_IOC_TYPE(cmd) != 'S' || _IOC_SIZE(cmd) != sizeof(int) ||
        !((_IOC_NR(cmd) >= 13 && _IOC_NR(cmd) <= 17) || (_IOC_NR(cmd) >= 19 && _IOC_NR(cmd) <= 27)))
        return -ENOIOCTLCMD;
    if (get_user(id, (int __user *)uarg))
        return -EFAULT;
    if (_IOC_NR(cmd) <= 17) {
        i = id - SEM_BASE;
        if (i < 0 || i >= SLOTS || !named_sem[i].name[0])
            return -EINVAL;
        switch (_IOC_NR(cmd)) {
        case 13:
        case 14:
            ret = _IOC_NR(cmd) == 13 ? down_interruptible(&named_sem[i].sem)
                                     : down_trylock(&named_sem[i].sem);
            if (!ret) {
                spin_lock_irqsave(&named_sem[i].lock, flags);
                named_sem[i].taken++;
                spin_unlock_irqrestore(&named_sem[i].lock, flags);
            }
            return ret;
        case 15:
        case 16:
            spin_lock_irqsave(&named_sem[i].lock, flags);
            if (_IOC_NR(cmd) == 15 && !named_sem[i].taken) {
                spin_unlock_irqrestore(&named_sem[i].lock, flags);
                return -EINVAL;
            }
            ret = _IOC_NR(cmd) == 16 ? named_sem[i].taken : 1;
            named_sem[i].taken -= ret;
            spin_unlock_irqrestore(&named_sem[i].lock, flags);
            while (ret--)
                up(&named_sem[i].sem);
            return 0;
        /* Cmd 17 controls the older named mutex's cross-thread unlock policy. */
        default:
            return -ENOTTY;
        }
    }
    i = id - RW_BASE;
    if (i < 0 || i >= SLOTS || !named_rw[i].name[0])
        return -EINVAL;
    switch (_IOC_NR(cmd)) {
    case 19:
        mutex_lock(&names_lock);
        named_rw[i].name[0] = 0;
        mutex_unlock(&names_lock);
        return 0;
    case 26:
    case 27:
        writer = _IOC_NR(cmd) == 27;
        spin_lock_irqsave(&named_rw[i].lock, flags);
        if ((writer && !named_rw[i].writer) || (!writer && !named_rw[i].readers)) {
            spin_unlock_irqrestore(&named_rw[i].lock, flags);
            return -EPERM;
        }
        if (writer)
            named_rw[i].writer = false;
        else
            named_rw[i].readers--;
        spin_unlock_irqrestore(&named_rw[i].lock, flags);
        wake_up_all(&named_rw[i].wait);
        return 0;
    default:
        writer = _IOC_NR(cmd) >= 23;
        if (_IOC_NR(cmd) == 20 || _IOC_NR(cmd) == 23)
            return rw_try(i, writer);
        if (_IOC_NR(cmd) == 21 || _IOC_NR(cmd) == 24) {
            if (get_user(wait_ms, (u32 __user *)uarg + 1))
                return -EFAULT;
            ret = wait_event_interruptible_timeout(named_rw[i].wait, rw_try(i, writer),
                                                   msecs_to_jiffies(wait_ms));
            return ret < 0 ? ret : ret > 0;
        }
        ret = wait_event_interruptible(named_rw[i].wait, rw_try(i, writer));
        return ret ? ret : 1;
    }
}

void semutex_new_init(void)
{
    for (int i = 0; i < SLOTS; i++) {
        spin_lock_init(&named_sem[i].lock);
        spin_lock_init(&named_rw[i].lock);
        init_waitqueue_head(&named_rw[i].wait);
    }
}
