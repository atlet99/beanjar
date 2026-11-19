// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Create a device-mapper linear device and a block node for it, from the initramfs, before webOS
 * starts. The initramfs busybox has no dmsetup, so this issues the device-mapper ioctls directly
 * (/dev/mapper/control, CONFIG_BLK_DEV_DM is built into the guest kernel).
 *
 * It is the ioctl half of guest/initramfs/rootfs/bin/beanjar-emmc, which builds the whole-disk
 * /dev/mmcblk0 the stock firmware updater writes into (firmware.updatable,
 * docs/research/firmware-update.md).
 *
 *   dm-linear <name> <node-path>
 *
 * reads the table from stdin, one segment per line:
 *   <start_sector> <length_sector> <major>:<minor> <src_sector>
 * creates the dm-linear device <name>, loads and resumes the table, then makes the block node
 * <node-path> with the new device's number (root:root 0600, as the eMMC nodes init makes). Every
 * number is in 512-byte sectors, as device-mapper counts. It prints nothing on success; errors go
 * to stderr. */
#include <errno.h>
#include <fcntl.h>
#include <linux/dm-ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define MAX_SEGMENTS 128
#define BUF_SIZE     (16 * 1024)

struct segment {
    uint64_t start;
    uint64_t length;
    char params[64]; /* "major:minor src_sector" */
};

static void init_ioctl(struct dm_ioctl *io, size_t size, const char *name)
{
    memset(io, 0, sizeof *io);
    io->version[0] = DM_VERSION_MAJOR;
    io->version[1] = DM_VERSION_MINOR;
    io->version[2] = DM_VERSION_PATCHLEVEL;
    io->data_size = size;
    io->data_start = sizeof *io;
    if (name)
        snprintf(io->name, sizeof io->name, "%s", name);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: dm-linear <name> <node-path>\n");
        return 2;
    }
    const char *name = argv[1];
    const char *node = argv[2];

    struct segment segs[MAX_SEGMENTS];
    int count = 0;
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == '\n' || line[0] == '\0')
            continue;
        if (count >= MAX_SEGMENTS) {
            fprintf(stderr, "dm-linear: more than %d segments\n", MAX_SEGMENTS);
            return 1;
        }
        unsigned long long start, length;
        unsigned maj, min;
        unsigned long long src;
        if (sscanf(line, "%llu %llu %u:%u %llu", &start, &length, &maj, &min, &src) != 5) {
            fprintf(stderr, "dm-linear: bad table line: %s", line);
            return 1;
        }
        segs[count].start = start;
        segs[count].length = length;
        snprintf(segs[count].params, sizeof segs[count].params, "%u:%u %llu", maj, min, src);
        count++;
    }
    if (count == 0) {
        fprintf(stderr, "dm-linear: empty table\n");
        return 1;
    }

    int fd = open("/dev/mapper/control", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "dm-linear: open /dev/mapper/control: %s\n", strerror(errno));
        return 1;
    }

    /* Create the device. */
    char buf[BUF_SIZE];
    struct dm_ioctl *io = (struct dm_ioctl *)buf;
    init_ioctl(io, sizeof buf, name);
    if (ioctl(fd, DM_DEV_CREATE, io) < 0) {
        fprintf(stderr, "dm-linear: DM_DEV_CREATE %s: %s\n", name, strerror(errno));
        return 1;
    }

    /* Load the table: dm_ioctl, then a dm_target_spec and null-terminated params per segment,
     * each padded so the next spec is 8-byte aligned. */
    init_ioctl(io, sizeof buf, name);
    io->target_count = count;
    char *p = buf + sizeof *io;
    for (int i = 0; i < count; i++) {
        struct dm_target_spec *spec = (struct dm_target_spec *)p;
        size_t plen = strlen(segs[i].params) + 1;
        size_t speclen = sizeof *spec + plen;
        speclen = (speclen + 7) & ~((size_t)7);
        if (p + speclen > buf + sizeof buf) {
            fprintf(stderr, "dm-linear: table too large\n");
            return 1;
        }
        memset(spec, 0, speclen);
        spec->sector_start = segs[i].start;
        spec->length = segs[i].length;
        spec->status = 0;
        spec->next = speclen;
        snprintf(spec->target_type, sizeof spec->target_type, "linear");
        memcpy((char *)spec + sizeof *spec, segs[i].params, plen);
        p += speclen;
    }
    if (ioctl(fd, DM_TABLE_LOAD, io) < 0) {
        fprintf(stderr, "dm-linear: DM_TABLE_LOAD %s: %s\n", name, strerror(errno));
        return 1;
    }

    /* Resume: a just-created device is suspended; this activates the loaded table. */
    init_ioctl(io, sizeof buf, name);
    if (ioctl(fd, DM_DEV_SUSPEND, io) < 0) {
        fprintf(stderr, "dm-linear: DM_DEV_SUSPEND %s: %s\n", name, strerror(errno));
        return 1;
    }

    /* The node, from the device number the ioctls report (io->dev). */
    unsigned maj = major(io->dev), min = minor(io->dev);
    unlink(node);
    if (mknod(node, S_IFBLK | 0600, makedev(maj, min)) < 0) {
        fprintf(stderr, "dm-linear: mknod %s %u:%u: %s\n", node, maj, min, strerror(errno));
        return 1;
    }
    close(fd);
    return 0;
}
