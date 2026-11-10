// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
#include "private.h"

/* ---- the GOP frame buffer (memory_gop_id()): include/mstar-gop.h ----
 *
 * SimpleSmart's graphics HAL (libhalgal) allocated its surfaces from the GOP pool through MStar's
 * frame buffer driver (/dev/fb0) and set the GOP's planes in hardware; LG's DirectFB maps the
 * pool from /dev/fb0 (its devmem system). Here our libhalgal does the drawing in software, and
 * this node is the hardware it leaves: the pool, which is the record's pages of phys_mem (what
 * /dev/malloc maps there too), a first-fit allocator of it, and the planes, whose changes the
 * presenter waits for (docs/dev/architecture/simplesmart.md, "Display"). Any other ioctl (fbdev's,
 * MStar's own) is not one this device has: -ENOTTY, counted. */

struct gop_block {
    struct list_head list;
    u32 pa, size;
};
static LIST_HEAD(gop_blocks);  /* the allocated ranges, by pa */
static DEFINE_MUTEX(gop_lock); /* the blocks and the planes */
static struct mstar_gop_plane gop_planes[MSTAR_GOP_PLANES];
static atomic_t gop_gen = ATOMIC_INIT(1);
static DECLARE_WAIT_QUEUE_HEAD(gop_waitq);

static u32 gop_pool_pa(void)
{
    return (u32)record_pa(memory_gop_id());
}

static void gop_bump(void)
{
    atomic_inc(&gop_gen);
    wake_up_interruptible_all(&gop_waitq);
}

/* The pool's first page is never allocated: LG's GX layer takes a surface's offset in the pool
 * as its handle, and 0 as no surface (libgmopenapi's window surface lookup). */
#define GOP_RESERVED PAGE_SIZE

static long gop_alloc(struct mstar_gop_alloc *a)
{
    u32 base = gop_pool_pa() + GOP_RESERVED,
        end = gop_pool_pa() + memory_record_length(memory_gop_id());
    u32 align = a->align ? a->align : 64;
    u64 at = base, size = ALIGN((u64)a->size, 64);
    struct gop_block *b, *nb;

    if (!a->size || a->size > end - base || (align & (align - 1)) || align > SZ_16M)
        return -EINVAL;
    nb = kmalloc(sizeof(*nb), GFP_KERNEL);
    if (!nb)
        return -ENOMEM;
    mutex_lock(&gop_lock);
    list_for_each_entry(b, &gop_blocks, list)
    {
        at = ALIGN(at, (u64)align);
        if (at + size <= b->pa)
            break;
        at = (u64)b->pa + b->size;
    }
    at = ALIGN(at, (u64)align);
    if (at + size > end) {
        mutex_unlock(&gop_lock);
        kfree(nb);
        return -ENOMEM;
    }
    nb->pa = at;
    nb->size = size;
    list_add_tail(&nb->list, &b->list); /* before b, or at the end (b is the head) */
    mutex_unlock(&gop_lock);
    a->pa = at;
    return 0;
}

static long gop_free(u32 pa)
{
    struct gop_block *b;

    mutex_lock(&gop_lock);
    list_for_each_entry(b, &gop_blocks, list)
    {
        if (b->pa == pa) {
            list_del(&b->list);
            mutex_unlock(&gop_lock);
            kfree(b);
            return 0;
        }
    }
    mutex_unlock(&gop_lock);
    return -EINVAL;
}

static long gop_set_plane(const struct mstar_gop_plane *p)
{
    u64 base = gop_pool_pa(), end = base + memory_record_length(memory_gop_id());

    if (p->index >= MSTAR_GOP_PLANES)
        return -EINVAL;
    if (p->enabled &&
        (p->pa < base || !p->pitch || !p->height || (u64)p->pa + (u64)p->pitch * p->height > end))
        return -EINVAL;
    mutex_lock(&gop_lock);
    gop_planes[p->index] = *p;
    gop_planes[p->index].gen = atomic_read(&gop_gen) + 1;
    mutex_unlock(&gop_lock);
    gop_bump();
    return 0;
}

static long gop_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    void __user *uarg = (void __user *)arg;

    ioctl_count("gop", cmd);
    switch (cmd) {
    case MSTAR_GOP_IOC_GET_POOL: {
        struct mstar_gop_pool p = {.pa = gop_pool_pa(),
                                   .size = memory_record_length(memory_gop_id())};
        return copy_to_user(uarg, &p, sizeof(p)) ? -EFAULT : 0;
    }
    case MSTAR_GOP_IOC_ALLOC: {
        struct mstar_gop_alloc a;
        long err;

        if (copy_from_user(&a, uarg, sizeof(a)))
            return -EFAULT;
        err = gop_alloc(&a);
        if (err)
            return err;
        if (copy_to_user(uarg, &a, sizeof(a))) {
            gop_free(a.pa);
            return -EFAULT;
        }
        return 0;
    }
    case MSTAR_GOP_IOC_FREE: {
        u32 pa;

        if (get_user(pa, (u32 __user *)uarg))
            return -EFAULT;
        return gop_free(pa);
    }
    case MSTAR_GOP_IOC_SET_PLANE: {
        struct mstar_gop_plane p;

        if (copy_from_user(&p, uarg, sizeof(p)))
            return -EFAULT;
        return gop_set_plane(&p);
    }
    case MSTAR_GOP_IOC_GET_PLANE: {
        struct mstar_gop_plane p;

        if (copy_from_user(&p, uarg, sizeof(p)))
            return -EFAULT;
        if (p.index >= MSTAR_GOP_PLANES)
            return -EINVAL;
        {
            u32 index = p.index;

            mutex_lock(&gop_lock);
            p = gop_planes[index];
            mutex_unlock(&gop_lock);
            p.index = index; /* a plane never set has all zeros */
        }
        return copy_to_user(uarg, &p, sizeof(p)) ? -EFAULT : 0;
    }
    case MSTAR_GOP_IOC_MEM_INFO: {
        struct mstar_gop_mem m = {.total = memory_record_length(memory_gop_id()) - GOP_RESERVED};
        u32 at = gop_pool_pa() + GOP_RESERVED,
            end = gop_pool_pa() + memory_record_length(memory_gop_id());
        struct gop_block *b;

        mutex_lock(&gop_lock);
        list_for_each_entry(b, &gop_blocks, list)
        {
            m.allocated += b->size;
            m.largest_free = max(m.largest_free, b->pa - at);
            at = b->pa + b->size;
        }
        mutex_unlock(&gop_lock);
        m.largest_free = max(m.largest_free, end - at);
        m.free = m.total - m.allocated;
        return copy_to_user(uarg, &m, sizeof(m)) ? -EFAULT : 0;
    }
    case MSTAR_GOP_IOC_DAMAGE:
        gop_bump();
        return 0;
    case MSTAR_GOP_IOC_WAIT: {
        struct mstar_gop_wait w;
        long left;

        if (copy_from_user(&w, uarg, sizeof(w)))
            return -EFAULT;
        left = wait_event_interruptible_timeout(gop_waitq, (u32)atomic_read(&gop_gen) != w.gen,
                                                msecs_to_jiffies(min(w.timeout_ms, 1000u)));
        if (left < 0)
            return left;
        w.gen = atomic_read(&gop_gen);
        return copy_to_user(uarg, &w, sizeof(w)) ? -EFAULT : 0;
    }
    }
    return unhandled("gop", cmd, -ENOTTY);
}

/* SS1A's stock users map the pool from its start two ways, and each adds a surface's offset:
 * LG's GX client (libgmopenapi's HOA_GX_GetDeviceCapability) at file offset 0, procmgr
 * (_PM_CURSOR_DRAW_GetVirtualGFXBaseAddr) at the capability's physicalAddrOfBase, the pool's PA.
 * So a file offset within the pool's size is an offset in the pool, and one within
 * [pool PA, pool PA + size) a PA in it (the two ranges do not meet: the pool is not at PA 0). */
static int gop_mmap(struct file *file, struct vm_area_struct *vma)
{
    u64 off = (u64)vma->vm_pgoff << PAGE_SHIFT, len = vma->vm_end - vma->vm_start;
    u64 base = gop_pool_pa(), size = memory_record_length(memory_gop_id());

    if (off >= base && off - base + len <= size)
        off -= base;
    else if (off + len > size)
        return -EINVAL;
    return lgemu_store_mmap(memory_store(), vma, base + off);
}

void gop_exit(void)
{
    struct gop_block *b, *n;

    list_for_each_entry_safe(b, n, &gop_blocks, list)
    {
        list_del(&b->list);
        kfree(b);
    }
}

const struct file_operations gop_fops = {
    .owner = THIS_MODULE,
    .open = plain_open,
    .mmap = gop_mmap,
    .unlocked_ioctl = gop_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
};
