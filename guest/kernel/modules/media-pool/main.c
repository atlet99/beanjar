// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/*
 * The media pool (include/webos-media.h, "The pool"): WEBOS_MEDIA_POOL_DEVICE over
 * QEMU's ivshmem-doorbell, which the kernel's webos-ivshmem binds early (patch 0018) for its
 * MSI-X vector. The media helper on the host owns the allocator and writes the directory in
 * granule 0; this module enforces it for the guest's processes.
 *
 * Open to everyone like the media ports (the web process and jailed native apps decode too, and
 * see only /dev/lg). Access goes by open file: CLAIM binds the file to one port, which no other
 * file can claim while it has it; EXPORT gives a CPU-only dma-buf of a range inside a LIVE extent
 * the host granted to that port; RING rings the host. The device has no mmap: the directory is
 * the kernel's alone. Each dma-buf counts as an export of its extent in the directory until it is
 * released, and the host frees a RETIRING extent only when its count is 0, so no range is reused
 * or punched while a process still maps it. poll() and read() follow the doorbell's interrupts.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/dma-buf.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/webos-ivshmem.h>
#include "grants.h"

static DEFINE_MUTEX(lock);
static phys_addr_t base;
static u32 pool_size;
static struct webos_media_pool_dir *dir; /* granule 0, write-back */
static const void *owner[WEBOS_MEDIA_PORTS];
static u32 claims[WEBOS_MEDIA_PORTS];
static u32 exports[WEBOS_MEDIA_PORTS][WEBOS_MEDIA_POOL_EXTENTS];
static atomic_t irqs = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(irq_wait);

struct pool_file {
    int port; /* claimed, -1: none */
    u32 seen; /* irqs at the last read */
};

struct pool_buf {
    phys_addr_t phys;
    size_t size;
    u32 port;
    int e;
};

static bool dir_valid(void)
{
    return READ_ONCE(dir->magic) == WEBOS_MEDIA_POOL_MAGIC &&
           READ_ONCE(dir->version) == WEBOS_MEDIA_POOL_VERSION;
}

static void ring_host(void)
{
    if (dir_valid())
        webos_ivshmem_ring(READ_ONCE(dir->host_peer), 0);
}

static void notify(void *unused)
{
    atomic_inc(&irqs);
    wake_up_interruptible(&irq_wait);
}

/* exports[port][e] into the directory, the host told when an extent it retires is unmapped. With
 * lock.
 */
static void publish_exports(u32 port, int e)
{
    WRITE_ONCE(dir->port[port].exports[e], exports[port][e]);
    /* the count before the state: pairs with pool_retire's barrier on the host */
    smp_mb();
    if (!exports[port][e] &&
        READ_ONCE(dir->port[port].extent[e].state) == WEBOS_MEDIA_EXTENT_RETIRING)
        ring_host();
}

static void publish_claim(u32 port)
{
    WRITE_ONCE(dir->port[port].claim, claims[port]);
}

static struct sg_table *map_dma_buf(struct dma_buf_attachment *a, enum dma_data_direction d)
{
    return ERR_PTR(-EOPNOTSUPP);
}

static void unmap_dma_buf(struct dma_buf_attachment *a, struct sg_table *sgt,
                          enum dma_data_direction d)
{
}

/* An export's end: its extent's count down. */
static void put_export(struct pool_buf *b)
{
    mutex_lock(&lock);
    if (!WARN_ON(!exports[b->port][b->e]))
        exports[b->port][b->e]--;
    publish_exports(b->port, b->e);
    mutex_unlock(&lock);
    kfree(b);
}

static void release_buf(struct dma_buf *dmabuf)
{
    put_export(dmabuf->priv);
}

static int mmap_buf(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
    struct pool_buf *b = dmabuf->priv;
    unsigned long len = vma->vm_end - vma->vm_start;

    if (vma->vm_pgoff > (b->size >> PAGE_SHIFT) || len > b->size - (vma->vm_pgoff << PAGE_SHIFT))
        return -EINVAL;
    vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
    return remap_pfn_range(vma, vma->vm_start, (b->phys >> PAGE_SHIFT) + vma->vm_pgoff, len,
                           vma->vm_page_prot);
}

static const struct dma_buf_ops buf_ops = {
    .map_dma_buf = map_dma_buf,
    .unmap_dma_buf = unmap_dma_buf,
    .release = release_buf,
    .mmap = mmap_buf,
};

static long do_export(struct pool_file *pf, struct webos_media_export __user *uarg)
{
    struct webos_media_export e;
    struct pool_buf *b;
    struct dma_buf *dmabuf;
    u32 gen;
    int x, fd;
    DEFINE_DMA_BUF_EXPORT_INFO(info);

    if (copy_from_user(&e, uarg, sizeof(e)))
        return -EFAULT;
    if (e.size > U32_MAX - PAGE_SIZE)
        return -EINVAL;
    b = kzalloc(sizeof(*b), GFP_KERNEL);
    if (!b)
        return -ENOMEM;
    mutex_lock(&lock);
    if (pf->port < 0) {
        x = -EPERM;
    } else if (!dir_valid()) {
        x = -ENODEV;
    } else {
        x = mp_grant_lookup(dir, pool_size, pf->port, e.offset, PAGE_ALIGN(e.size), &gen);
        if (x >= 0) {
            /* counted first, then the state again: the host retires
             * first, then reads the count (both with a full barrier), so
             * one of the two sees the other
             */
            exports[pf->port][x]++;
            publish_exports(pf->port, x);
            if (!mp_extent_live(dir, pf->port, x, gen)) {
                exports[pf->port][x]--;
                publish_exports(pf->port, x);
                x = -EPERM;
            }
        }
    }
    if (x < 0) {
        mutex_unlock(&lock);
        kfree(b);
        return x;
    }
    b->port = pf->port;
    b->e = x;
    b->phys = base + e.offset;
    b->size = PAGE_ALIGN(e.size);
    mutex_unlock(&lock);
    info.ops = &buf_ops;
    info.size = b->size;
    info.flags = O_RDWR | O_CLOEXEC;
    info.priv = b;
    dmabuf = dma_buf_export(&info);
    if (IS_ERR(dmabuf)) {
        put_export(b);
        return PTR_ERR(dmabuf);
    }
    fd = dma_buf_fd(dmabuf, O_CLOEXEC);
    if (fd < 0) {
        dma_buf_put(dmabuf);
        return fd;
    }
    /* the fd is installed: a failed copy leaves it to the caller's exit */
    if (put_user(fd, &uarg->fd))
        return -EFAULT;
    return 0;
}

static long do_claim(struct file *file, struct pool_file *pf, u32 __user *uport)
{
    u32 port;
    int old, ret;

    if (get_user(port, uport))
        return -EFAULT;
    mutex_lock(&lock);
    old = pf->port;
    ret = mp_claim(owner, claims, file, &pf->port, port);
    if (!ret && old != pf->port) {
        if (old >= 0)
            publish_claim(old);
        publish_claim(port);
    }
    mutex_unlock(&lock);
    if (!ret && old >= 0 && old != pf->port)
        ring_host(); /* the old port's session lost its reader */
    return ret;
}

static long pool_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct pool_file *pf = file->private_data;

    switch (cmd) {
    case WEBOS_MEDIA_IOC_CLAIM:
        return do_claim(file, pf, (u32 __user *)arg);
    case WEBOS_MEDIA_IOC_EXPORT:
        return do_export(pf, (struct webos_media_export __user *)arg);
    case WEBOS_MEDIA_IOC_RING:
        if (READ_ONCE(pf->port) < 0)
            return -EPERM;
        ring_host();
        return 0;
    }
    return -ENOTTY;
}

static __poll_t pool_poll(struct file *file, poll_table *wait)
{
    struct pool_file *pf = file->private_data;

    poll_wait(file, &irq_wait, wait);
    return (u32)atomic_read(&irqs) != READ_ONCE(pf->seen) ? EPOLLIN | EPOLLRDNORM : 0;
}

static ssize_t pool_read(struct file *file, char __user *buf, size_t n, loff_t *off)
{
    struct pool_file *pf = file->private_data;
    u32 now = atomic_read(&irqs);

    if (n < sizeof(now))
        return -EINVAL;
    WRITE_ONCE(pf->seen, now);
    return copy_to_user(buf, &now, sizeof(now)) ? -EFAULT : sizeof(now);
}

static int pool_open(struct inode *inode, struct file *file)
{
    struct pool_file *pf = kzalloc(sizeof(*pf), GFP_KERNEL);

    if (!pf)
        return -ENOMEM;
    pf->port = -1;
    pf->seen = atomic_read(&irqs);
    file->private_data = pf;
    return stream_open(inode, file);
}

static int pool_release(struct inode *inode, struct file *file)
{
    struct pool_file *pf = file->private_data;
    int port;

    mutex_lock(&lock);
    port = mp_unclaim(owner, claims, file, &pf->port);
    if (port >= 0)
        publish_claim(port);
    mutex_unlock(&lock);
    if (port >= 0)
        ring_host(); /* the port's session lost its reader: its pool goes back */
    kfree(pf);
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = pool_open,
    .release = pool_release,
    .read = pool_read,
    .poll = pool_poll,
    .unlocked_ioctl = pool_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "webos.media-pool",
    .nodename = "lg/webos.media-pool",
    .fops = &fops,
    .mode = 0666,
};

static int __init media_pool_init(void)
{
    resource_size_t len;
    int err = webos_ivshmem_attach(notify, NULL, &base, &len);

    if (err) {
        pr_info("no pool (%d): pictures come inline\n", err);
        return err;
    }
    if (len < 2 * WEBOS_MEDIA_POOL_GRANULE || len > WEBOS_MEDIA_POOL_SIZE) {
        pr_warn("BAR of %llu bytes is not a pool\n", (unsigned long long)len);
        err = -ENODEV;
        goto detach;
    }
    pool_size = len;
    dir = memremap(base, WEBOS_MEDIA_POOL_GRANULE, MEMREMAP_WB);
    if (!dir) {
        err = -ENOMEM;
        goto detach;
    }
    /* the counts and claims start from 0 again: whatever a previous load left is void */
    memset(claims, 0, sizeof(claims));
    memset(exports, 0, sizeof(exports));
    if (dir_valid()) {
        int p, e;

        for (p = 0; p < WEBOS_MEDIA_PORTS; p++) {
            for (e = 0; e < WEBOS_MEDIA_POOL_EXTENTS; e++)
                WRITE_ONCE(dir->port[p].exports[e], 0);
            /* an even count past the old one: the host sees every old claim end */
            claims[p] = (READ_ONCE(dir->port[p].claim) + 2) & ~1u;
            publish_claim(p);
        }
        ring_host();
    }
    err = misc_register(&misc);
    if (err)
        goto unmap;
    pr_info("%u MiB at %pa%s\n", pool_size >> 20, &base,
            dir_valid() ? "" : " (the host has not written the directory)");
    return 0;
unmap:
    memunmap(dir);
    dir = NULL;
detach:
    webos_ivshmem_detach();
    return err;
}

static void __exit media_pool_exit(void)
{
    misc_deregister(&misc);
    webos_ivshmem_detach();
    memunmap(dir);
    dir = NULL;
}

module_init(media_pool_init);
module_exit(media_pool_exit);
MODULE_DESCRIPTION("The webOS emulator's media pool");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("DMA_BUF");
