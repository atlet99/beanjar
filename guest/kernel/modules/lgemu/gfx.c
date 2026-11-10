// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* The graphics memory behind /dev/gfx (include/webos-gfx.h): what the SoC's gfx driver gives user
 * space on a TV, here for the soc layer's DILE_GAL (guest/layers/soc/src/libdile_gal/gfx-memory.c),
 * which every GM client, the compositor and tvservice map. One shmem object of LGEMU_GFX_SIZE
 * bytes, shared by every mapping of every node that uses lgemu_gfx_fops: its pages cost memory once
 * written and stay while the module is loaded (a gm restart finds its clients' surfaces). Shared
 * mappings only; they are ordinary shmem pages, so futexes and flock work on them.
 *
 * The node is the SoC module's, where the vendor's driver makes it (lgemu-drv.h): lg115x.ko's LG
 * gfx driver /dev/lg/gfx0 (151:0), rtk.ko's Realtek gal_gfx /dev/gfx, mstar-utopia.ko's /dev/gfx.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": gfx: " fmt
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/shmem_fs.h>
#include "lgemu.h"
#include "lgemu-drv.h"

static struct file *gfx_file;

static int gfx_mmap(struct file *f, struct vm_area_struct *vma)
{
    if (!(vma->vm_flags & VM_SHARED))
        return -EINVAL;
    if (vma->vm_pgoff > LGEMU_GFX_SIZE >> PAGE_SHIFT ||
        vma_pages(vma) > (LGEMU_GFX_SIZE >> PAGE_SHIFT) - vma->vm_pgoff)
        return -EINVAL;
    vma_set_file(vma, gfx_file);
    return vfs_mmap(gfx_file, vma);
}

const struct file_operations lgemu_gfx_fops = {
    .owner = THIS_MODULE,
    .open = nonseekable_open,
    .mmap = gfx_mmap,
};
EXPORT_SYMBOL_GPL(lgemu_gfx_fops);

int lgemu_gfx_init(void)
{
    gfx_file = shmem_file_setup("lgemu-gfx", LGEMU_GFX_SIZE, VM_NORESERVE);
    if (IS_ERR(gfx_file)) {
        int err = PTR_ERR(gfx_file);

        gfx_file = NULL;
        return err;
    }
    return 0;
}

void lgemu_gfx_exit(void)
{
    if (gfx_file)
        fput(gfx_file);
    gfx_file = NULL;
}
