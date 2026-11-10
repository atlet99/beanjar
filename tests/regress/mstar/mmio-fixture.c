// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* Synthetic device for validating the fault transport, never an LG register ABI.
 * First word is W1C, second ordinary data; invalid offsets have no side effects. */
#include "../../kernel/modules/lgemu/lgemu-drv.h"

static DEFINE_SPINLOCK(lock);
static u32 status = 0xffffffff, data;
static bool trap(u64 page)
{
    return page == 0;
}
static int access_reg(u64 addr, unsigned int width, bool write, u32 *value)
{
    u32 mask, *reg;
    unsigned int shift;

    if (addr >= 8 || (addr & 3) + width > 4)
        return -EINVAL;
    reg = addr < 4 ? &status : &data;
    shift = (addr & 3) * 8;
    mask = width == 4 ? ~0U : (1U << (width * 8)) - 1;
    spin_lock(&lock);
    if (!write)
        *value = (*reg >> shift) & mask;
    else if (addr < 4)
        *reg &= ~((*value & mask) << shift);
    else
        *reg = (*reg & ~(mask << shift)) | ((*value & mask) << shift);
    spin_unlock(&lock);
    return 0;
}
static struct lgemu_store store = {
    .pages = XARRAY_INIT(store.pages, 0),
    .owner = THIS_MODULE,
    .trap_page = trap,
    .access = access_reg,
};
static struct lgemu_cdev dev;
static int map(struct file *f, struct vm_area_struct *vma)
{
    if (vma->vm_pgoff || vma->vm_end - vma->vm_start != PAGE_SIZE)
        return -EINVAL;
    return lgemu_store_mmap(&store, vma, 0);
}
static const struct file_operations ops = {.owner = THIS_MODULE, .mmap = map};
static int __init init_fixture(void)
{
    return lgemu_cdev_add(&dev, 0, 0, 1, "mmio-fixture", &ops);
}
static void __exit exit_fixture(void)
{
    lgemu_cdev_del(&dev);
    lgemu_store_free(&store);
}
module_init(init_fixture);
module_exit(exit_fixture);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synthetic lgemu synchronous MMIO transport test device");
