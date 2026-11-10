// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/* lgfs.ko: LG's names for the USB storage filesystems, tfat, tntfs and ntfs, on the kernel's own
 * vfat and ntfs3. The stock PDM (physical-device-manager) of webOS 1 to 23 mounts a FAT partition
 * with fstype "tfat" and an NTFS one with "tntfs", LG's (Tuxera's) drivers, with their own options
 * (lgfs-options.h), and does not fall back to vfat when the type is unknown. The firmware's
 * tfat.ko and tntfs.ko are 32-bit ARM modules for LG's kernel; this arm64 kernel cannot load them.
 * webOS 24 on mounts NTFS as "ntfs" with LG's "nls" option, once, with no fallback: the
 * stock kernel's built-in "ntfs" is LG's read-write driver on fs/ntfs, whose character set option
 * is nls. ntfs3's own legacy "ntfs" rejects nls, so the kernel is built without it
 * (CONFIG_NTFS3_FS, not CONFIG_NTFS_FS) and the name is this module's.
 *
 * Each type here is the kernel filesystem it names, with LG's options filtered: its
 * init_fs_context is the backend's (get_fs_type("vfat"), "ntfs3"), and the context operations the
 * backend installs are copied once with parse_param wrapped, which drops or renames LG's options
 * and hands every other one to the backend. Superblocks are the backend's in every respect but
 * s_type, which is this module's type: /proc/mounts names "tfat" as on the TV, statfs and every
 * file operation are vfat's, and kill_sb is the backend's. tntfs is read-only, as ntfs3's legacy
 * "ntfs" was when this module took tntfs on: get_tree and reconfigure here keep SB_RDONLY set.
 * "ntfs" writes, as the stock webOS 24 driver does. Loaded per release line with the peripheral
 * modules (boot.conf's PERIPHERAL_MODULES, after vfat and ntfs3); the backend is looked up at the
 * first mount and held until this module goes, and a mounted type holds this module.
 * docs/dev/architecture/build-and-boot.md, "Peripheral modules". */
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "lgfs-options.h"

struct lgfs_type {
    struct file_system_type type;
    const char *backend_name;
    const struct lgfs_option *options;
    bool read_only;
    /* set at the first mount, under lgfs_lock, and kept until the module exits */
    struct file_system_type *backend; /* holds a reference on its module */
    const struct fs_context_operations *backend_ops;
    struct fs_context_operations
        ops; /* backend_ops with parse_param (get_tree, reconfigure) ours */
};

static DEFINE_MUTEX(lgfs_lock);

static struct lgfs_type *lgfs_of(struct file_system_type *type)
{
    return container_of(type, struct lgfs_type, type);
}

static int lgfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
    struct lgfs_type *t = lgfs_of(fc->fs_type);
    const struct lgfs_option *o = lgfs_option(t->options, param->key);
    struct fs_parameter renamed;
    int ret;

    if (!o)
        return t->backend_ops->parse_param(fc, param);
    if (o->action == LGFS_DROP)
        return 0;
    renamed = *param;
    renamed.key = o->to;
    ret = t->backend_ops->parse_param(fc, &renamed);
    /* the backend may have taken the value (ntfs3 keeps iocharset's string) */
    param->string = renamed.string;
    return ret;
}

static int lgfs_get_tree(struct fs_context *fc)
{
    struct lgfs_type *t = lgfs_of(fc->fs_type);

    /* after the options: an "rw" among them clears what init_fs_context set */
    fc->sb_flags |= SB_RDONLY;
    return t->backend_ops->get_tree(fc);
}

static int lgfs_reconfigure(struct fs_context *fc)
{
    struct lgfs_type *t = lgfs_of(fc->fs_type);

    fc->sb_flags |= SB_RDONLY; /* a remount rw stays read-only */
    return t->backend_ops->reconfigure ? t->backend_ops->reconfigure(fc) : 0;
}

static struct file_system_type *lgfs_backend(struct lgfs_type *t)
{
    struct file_system_type *fs;

    mutex_lock(&lgfs_lock);
    if (!t->backend) {
        fs = get_fs_type(t->backend_name);
        if (fs && !fs->init_fs_context) {
            module_put(fs->owner);
            fs = NULL;
        }
        t->backend = fs;
    }
    fs = t->backend;
    mutex_unlock(&lgfs_lock);
    if (!fs)
        pr_warn_ratelimited("lgfs: %s: no filesystem %s\n", t->type.name, t->backend_name);
    return fs;
}

/* The backend's context operations, the first time they are seen, with ours in place. */
static int lgfs_wrap(struct lgfs_type *t, const struct fs_context_operations *ops)
{
    int ret = 0;

    mutex_lock(&lgfs_lock);
    if (!t->backend_ops) {
        if (!ops || !ops->parse_param || !ops->get_tree || ops->parse_monolithic) {
            ret = -EINVAL;
        } else {
            t->backend_ops = ops;
            t->ops = *ops;
            t->ops.parse_param = lgfs_parse_param;
            if (t->read_only) {
                t->ops.get_tree = lgfs_get_tree;
                t->ops.reconfigure = lgfs_reconfigure;
            }
        }
    } else if (t->backend_ops != ops) {
        ret = -EINVAL;
    }
    mutex_unlock(&lgfs_lock);
    if (ret)
        pr_warn("lgfs: %s: %s's context operations cannot be wrapped\n", t->type.name,
                t->backend_name);
    return ret;
}

static int lgfs_init_fs_context(struct fs_context *fc)
{
    struct lgfs_type *t = lgfs_of(fc->fs_type);
    struct file_system_type *backend = lgfs_backend(t);
    int ret;

    if (!backend)
        return -ENODEV;
    ret = backend->init_fs_context(fc);
    if (ret)
        return ret;
    /* on failure the caller frees the context through the backend's own ops->free */
    ret = lgfs_wrap(t, fc->ops);
    if (ret)
        return ret;
    fc->ops = &t->ops;
    if (t->read_only)
        fc->sb_flags |= SB_RDONLY;
    return 0;
}

static void lgfs_kill_sb(struct super_block *sb)
{
    lgfs_of(sb->s_type)->backend->kill_sb(sb);
}

static struct lgfs_type lgfs_types[] = {
    {
        .type = {.name = "tfat"},
        .backend_name = "vfat",
        .options = lgfs_tfat_options,
    },
    {
        .type = {.name = "tntfs"},
        .backend_name = "ntfs3",
        .options = lgfs_tntfs_options,
        .read_only = true,
    },
    {
        .type = {.name = "ntfs"},
        .backend_name = "ntfs3",
        .options = lgfs_ntfs_options,
    },
};
MODULE_ALIAS_FS("tfat");
MODULE_ALIAS_FS("tntfs");
MODULE_ALIAS_FS("ntfs");

static void lgfs_unregister(int n)
{
    while (n--) {
        unregister_filesystem(&lgfs_types[n].type);
        if (lgfs_types[n].backend)
            module_put(lgfs_types[n].backend->owner);
    }
}

static int __init lgfs_init(void)
{
    int i, ret;

    for (i = 0; i < ARRAY_SIZE(lgfs_types); i++) {
        struct file_system_type *type = &lgfs_types[i].type;

        type->owner = THIS_MODULE;
        type->fs_flags = FS_REQUIRES_DEV;
        type->init_fs_context = lgfs_init_fs_context;
        type->kill_sb = lgfs_kill_sb;
        ret = register_filesystem(type);
        if (ret) {
            lgfs_unregister(i);
            return ret;
        }
    }
    return 0;
}

static void __exit lgfs_exit(void)
{
    lgfs_unregister(ARRAY_SIZE(lgfs_types));
}

module_init(lgfs_init);
module_exit(lgfs_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LG's tfat, tntfs and ntfs filesystem names on the kernel's vfat and ntfs3");
