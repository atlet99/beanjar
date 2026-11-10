/* SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors */
/* SPDX-License-Identifier: GPL-2.0-only */
/* The mount options LG's own filesystems take that the kernel's do not, as the stock PDM passes
 * them (lgfs.c). Each is accepted and dropped, or renamed to the kernel driver's own option. Plain
 * C on purpose: tests/unit/kernel/test_lgfs_options.py checks every PDM option string against these
 * tables and the kernel drivers' parameter lists. The includer brings strcmp(). */
#ifndef LGFS_OPTIONS_H
#define LGFS_OPTIONS_H

enum lgfs_action {
    LGFS_DROP,   /* accepted, with any value or none, and ignored */
    LGFS_RENAME, /* passed on under the kernel driver's name, value unchanged */
};

struct lgfs_option {
    const char *key;
    enum lgfs_action action;
    const char *to; /* LGFS_RENAME */
};

/* tfat (Tuxera's FAT): "iocharset=utf8,fastmount=1,max_prealloc_size=32m,uid=0,gid=5000,umask=0002"
 * from PDM. fastmount defers the free-cluster count, max_prealloc_size caps the cluster
 * preallocation of a file being written: tuning of LG's driver with no counterpart in vfat. */
static const struct lgfs_option lgfs_tfat_options[] = {
    {"fastmount", LGFS_DROP, 0},
    {"max_prealloc_size", LGFS_DROP, 0},
    {0, LGFS_DROP, 0},
};

/* tntfs (Tuxera's NTFS): "nls=utf8,max_prealloc_size=64m,uid=0,gid=5000,umask=0002" from PDM. Its
 * nls is the character set, ntfs3's iocharset. */
static const struct lgfs_option lgfs_tntfs_options[] = {
    {"max_prealloc_size", LGFS_DROP, 0},
    {"nls", LGFS_RENAME, "iocharset"},
    {0, LGFS_DROP, 0},
};

/* ntfs, webOS 24 on: "uid=0,gid=5000,umask=0002,nls=utf8" from PDM, the options of the stock
 * kernel's fs/ntfs-based driver, whose nls is ntfs3's iocharset. */
static const struct lgfs_option lgfs_ntfs_options[] = {
    {"nls", LGFS_RENAME, "iocharset"},
    {0, LGFS_DROP, 0},
};

static inline const struct lgfs_option *lgfs_option(const struct lgfs_option *table,
                                                    const char *key)
{
    for (; table->key; table++)
        if (!strcmp(table->key, key))
            return table;
    return 0;
}

#endif
