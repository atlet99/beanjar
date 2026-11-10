// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Secondary memory framebuffer and GAL graphics node. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/anon_inodes.h>
#include <linux/bitmap.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/unaligned.h>

#include "lgemu-drv.h"
#include "lgemu-i2c.h"
#include "rtk-internal.h"
#include "../../../../include/webos-video.h"

/* The secondary screen's frame buffer (below, "FB"): node:widthxheight, the /dev/fb<node> the
 * product's compositor names besides fb0 (W20L's product.env QT_QPA_EGLFS_FB=/dev/fb0:/dev/fb1). */
static char *fb;
module_param(fb, charp, 0444);
MODULE_PARM_DESC(fb, "the secondary screen's frame buffer, fbnode:widthxheight (32 bpp)");

/* ---- FB: the secondary screen's frame buffer ----
 * Realtek's venusfb (mali_fb.ko, a 4.4 module the VM cannot load) gives the TV fb0, the OSD, and
 * fb1, the secondary screen product.env's compositor opens as its second display. Here fb0 is
 * virtio-gpu's (the DRM fbdev emulation, registered at its probe, before this module); this is
 * fb1: memory, 32 bpp ARGB, which the compositor may draw into and nothing shows. Its private
 * ioctls, venusfb's FBEXTIO_PUT_DISPLAYPOSITION/VISIBLE/REGION (_IOC(0, 'n', 1..3, 4): the
 * starfish QPA's 0x46e01..3), are accepted. Registered only when it gets the node asked for. */
static struct fb_info *fb_info;
static void *fb_mem;

static int fb_mmap_mem(struct fb_info *info, struct vm_area_struct *vma)
{
    return remap_vmalloc_range(vma, info->screen_buffer, vma->vm_pgoff);
}

static int fb_ext_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
    if ((cmd & ~0x3u) == 0x46e00 && (cmd & 0x3))
        return 0;
    return -ENOTTY;
}

static int fb_check(struct fb_var_screeninfo *var, struct fb_info *info)
{
    *var = info->var;
    return 0;
}

static const struct fb_ops fb_mem_ops = {
    .owner = THIS_MODULE,
    __FB_DEFAULT_SYSMEM_OPS_RDWR,
    __FB_DEFAULT_SYSMEM_OPS_DRAW,
    .fb_check_var = fb_check,
    .fb_ioctl = fb_ext_ioctl,
    .fb_mmap = fb_mmap_mem,
};

int rtk_fb_setup(void)
{
    unsigned int node, w, h;
    size_t size;
    int err;

    if (!fb || !*fb)
        return 0;
    if (sscanf(fb, "%u:%ux%u", &node, &w, &h) != 3 || !w || !h || w > 8192 || h > 8192) {
        pr_err("fb=%s: fbnode:widthxheight\n", fb);
        return -EINVAL;
    }
    size = PAGE_ALIGN((size_t)w * h * 4);
    fb_mem = vmalloc_user(size);
    fb_info = framebuffer_alloc(0, NULL);
    if (!fb_mem || !fb_info)
        return -ENOMEM;
    fb_info->fbops = &fb_mem_ops;
    fb_info->flags = FBINFO_VIRTFB;
    fb_info->screen_buffer = fb_mem;
    fb_info->screen_size = size;
    strscpy(fb_info->fix.id, "rtk fb1", sizeof(fb_info->fix.id));
    fb_info->fix.type = FB_TYPE_PACKED_PIXELS;
    fb_info->fix.visual = FB_VISUAL_TRUECOLOR;
    fb_info->fix.line_length = w * 4;
    fb_info->fix.smem_len = size;
    fb_info->var.xres = fb_info->var.xres_virtual = w;
    fb_info->var.yres = fb_info->var.yres_virtual = h;
    fb_info->var.bits_per_pixel = 32;
    fb_info->var.red = (struct fb_bitfield){16, 8, 0};
    fb_info->var.green = (struct fb_bitfield){8, 8, 0};
    fb_info->var.blue = (struct fb_bitfield){0, 8, 0};
    fb_info->var.transp = (struct fb_bitfield){24, 8, 0};
    fb_info->var.activate = FB_ACTIVATE_NOW;
    err = register_framebuffer(fb_info);
    if (err) {
        framebuffer_release(fb_info);
        fb_info = NULL;
        return err;
    }
    if (fb_info->node != node) {
        pr_info("fb: got /dev/fb%d, not fb%u (no frame buffer before it?): not kept\n",
                fb_info->node, node);
        unregister_framebuffer(fb_info);
        framebuffer_release(fb_info);
        fb_info = NULL;
        return 0;
    }
    pr_info("fb%u: %ux%u, 32 bpp, shown nowhere\n", node, w, h);
    return 0;
}

void rtk_fb_remove(void)
{
    if (fb_info) {
        unregister_framebuffer(fb_info);
        framebuffer_release(fb_info);
    }
    fb_info = NULL;
    vfree(fb_mem);
    fb_mem = NULL;
}

/* ---- the GAL gfx driver: /dev/gfx ---- */

static char *gal_gfx_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666;
    return NULL;
}

static const struct class gal_gfx_class = {.name = "gal_gfx", .devnode = gal_gfx_devnode};
static struct lgemu_cdev gal_gfx_cdev;
static bool gal_gfx_made;

void rtk_gal_gfx_remove(void)
{
    if (gal_gfx_made) {
        device_destroy(&gal_gfx_class, gal_gfx_cdev.dev);
        class_unregister(&gal_gfx_class);
    }
    gal_gfx_made = false;
    lgemu_cdev_del(&gal_gfx_cdev);
}

int rtk_gal_gfx_add(void)
{
    struct device *d;
    int err = lgemu_cdev_add(&gal_gfx_cdev, 0, 0, 1, "gal_gfx", &lgemu_gfx_fops);

    if (err)
        return err;
    err = class_register(&gal_gfx_class);
    if (err)
        return err;
    d = device_create(&gal_gfx_class, NULL, gal_gfx_cdev.dev, NULL, "gfx");
    if (IS_ERR(d)) {
        class_unregister(&gal_gfx_class);
        return PTR_ERR(d);
    }
    gal_gfx_made = true;
    return 0;
}
