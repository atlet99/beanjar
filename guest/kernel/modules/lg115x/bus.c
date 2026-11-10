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

/* ---- the I2C buses ---- */

#define I2C_MAJOR      154
#define I2C_HW_MINOR   0
#define I2C_HW_BUSES   16
#define I2C_GPIO_MINOR 64
#define I2C_GPIO_BUSES 4
#define I2C_MINORS     (I2C_GPIO_MINOR + I2C_GPIO_BUSES)

static char *i2c[16];
static int i2c_count;
module_param_array(i2c, charp, &i2c_count, 0444);
MODULE_PARM_DESC(i2c, "the board's I2C devices, device:bus:slave (micom, nvm, amp; bus = the "
                      "minor of /dev/lg/iic<n>, 64-67 for iicGPIO; the 8-bit slave address)");

/* The device at each (bus, slave), from i2c; LGEMU_I2C_NONE elsewhere. */
static u8 i2c_map[I2C_MINORS][256];
/* A (bus, slave) that has not acknowledged, logged once. */
static unsigned long i2c_nacked[I2C_MINORS][256 / BITS_PER_LONG];

/* LX_I2C_RW_DATA_T of i2c_kapi.h from a 32-bit process, 28 bytes */
struct lx_i2c_rw_data32 {
    u8 slave;    /* the 8-bit slave address */
    u8 sub_size; /* the sub-address length */
    u8 sub[8];   /* the sub-address (the register or command) */
    u32 buf;     /* the data, a user pointer */
    u32 size;    /* its length */
    u32 clock;   /* LX_I2C_CLOCK_T; I2C_CLOCK_INVALID keeps the bus's */
    u32 flag;
};
static_assert(sizeof(struct lx_i2c_rw_data32) == 28);

#define I2C_IOC_MAGIC         'u'
#define I2C_IOW_SET_CLOCK     _IOW(I2C_IOC_MAGIC, 0, u32)
#define I2C_IOW_WRITE         _IOW(I2C_IOC_MAGIC, 1, struct lx_i2c_rw_data32)
#define I2C_IORW_READ         _IOWR(I2C_IOC_MAGIC, 2, struct lx_i2c_rw_data32)
#define I2C_IOW_SET_GPIO_PORT _IOW(I2C_IOC_MAGIC, 3, u16) /* LX_I2C_GPIO_MAP_T: sda, scl */
#define I2C_MAX_TRANSFER      (32 * 1024)                 /* i2c_drv.c's limit */

static int lx_i2c_parse(void)
{
    for (int i = 0; i < i2c_count; i++) {
        unsigned int at[2]; /* bus, slave */
        enum lgemu_i2c_device dev;

        if (lgemu_i2c_parse(i2c[i], &dev, at, 2) || at[0] >= I2C_MINORS ||
            (at[0] >= I2C_HW_BUSES && at[0] < I2C_GPIO_MINOR) || at[1] > 0xff) {
            pr_err("i2c=%s: not device:bus:slave\n", i2c[i]);
            return -EINVAL;
        }
        i2c_map[at[0]][at[1]] = dev;
        pr_info("i2c: %s at iic%s%u, 0x%02x\n", i2c[i], at[0] >= I2C_GPIO_MINOR ? "GPIO" : "",
                at[0] >= I2C_GPIO_MINOR ? at[0] - I2C_GPIO_MINOR : at[0], at[1]);
    }
    return 0;
}

static long lx_i2c_transfer(unsigned int bus, unsigned int cmd, void __user *uarg)
{
    struct lx_i2c_rw_data32 rw;
    enum lgemu_i2c_device dev;
    long ret;

    if (copy_from_user(&rw, uarg, sizeof(rw)))
        return -EFAULT;
    if (rw.size > I2C_MAX_TRANSFER)
        return -EFAULT;
    if (rw.sub_size > sizeof(rw.sub))
        return -EINVAL;
    dev = i2c_map[bus][rw.slave];
    if (dev == LGEMU_I2C_NONE) {
        if (!test_and_set_bit(rw.slave, i2c_nacked[bus]))
            pr_info("iic%u: nothing at 0x%02x (not acknowledged)\n", bus, rw.slave);
        return -EREMOTEIO;
    }
    /* On the wire: a write is the sub-address and the data in one message; a read writes the
     * sub-address (if any) and reads with a repeated start (i2c_core.c's _I2C_ReadCmd). */
    ret = lgemu_i2c_user_xfer(dev, rw.slave >> 1, rw.sub, rw.sub_size, compat_ptr(rw.buf), rw.size,
                              cmd != I2C_IOW_WRITE);
    return ret < 0 ? ret : rw.size; /* the byte count, as LG's driver */
}

static int lx_i2c_open(struct inode *inode, struct file *file)
{
    return 0;
}

static long lx_i2c_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    unsigned int bus = iminor(file_inode(file));

    switch (cmd) {
    case I2C_IOW_SET_CLOCK:
    case I2C_IOW_SET_GPIO_PORT:
        return 0;
    case I2C_IOW_WRITE:
    case I2C_IORW_READ:
        return lx_i2c_transfer(bus, cmd, compat_ptr(arg));
    }
    return -ENOTTY;
}

static const struct file_operations lx_i2c_fops = {
    .owner = THIS_MODULE,
    .open = lx_i2c_open,
    .unlocked_ioctl = lx_i2c_ioctl,
    .compat_ioctl = lx_i2c_ioctl,
};

static struct {
    unsigned int minor, count;
    const char *name;
    struct lgemu_cdev cdev;
} i2c_regions[] = {
    {I2C_HW_MINOR, I2C_HW_BUSES, "iic"},
    {I2C_GPIO_MINOR, I2C_GPIO_BUSES, "iicGPIO"},
};

void lx_i2c_remove(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(i2c_regions); i++)
        lgemu_cdev_del(&i2c_regions[i].cdev);
}

int lx_i2c_add(void)
{
    int err = lx_i2c_parse();

    for (size_t i = 0; !err && i < ARRAY_SIZE(i2c_regions); i++)
        err = lgemu_cdev_add(&i2c_regions[i].cdev, I2C_MAJOR, i2c_regions[i].minor,
                             i2c_regions[i].count, i2c_regions[i].name, &lx_i2c_fops);
    if (err)
        lx_i2c_remove();
    return err;
}

/* ---- the SPI bus: spidev0.<cs> ---- */

#define SPI_MAJOR  153 /* spidev's */
#define SPI_MAX_CS 4
#define SPI_BUFSIZ 4096 /* spidev's default bufsiz: the longest transfer */

static unsigned int spi_cs;
module_param(spi_cs, uint, 0444);
MODULE_PARM_DESC(spi_cs, "the SPI master's chip selects, /dev/spidev0.0 on (0: no SPI bus)");

struct lx_spi_file {
    u32 mode, speed;
    u8 bits;
};

static int lx_spi_open(struct inode *inode, struct file *file)
{
    struct lx_spi_file *f = kzalloc(sizeof(*f), GFP_KERNEL);

    if (!f)
        return -ENOMEM;
    f->bits = 8;
    f->speed = 500000;
    file->private_data = f;
    return 0;
}

static int lx_spi_release(struct inode *inode, struct file *file)
{
    kfree(file->private_data);
    return 0;
}

/* A half-duplex receive: nothing on the bus drives MISO. */
static ssize_t lx_spi_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    if (count > SPI_BUFSIZ)
        return -EMSGSIZE;
    return clear_user(buf, count) ? -EFAULT : count;
}

static ssize_t lx_spi_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
    return count > SPI_BUFSIZ ? -EMSGSIZE : count;
}

static void __user *lx_spi_ptr(u64 p)
{
    return in_compat_syscall() ? compat_ptr((u32)p) : u64_to_user_ptr(p);
}

static long lx_spi_message(void __user *uarg, unsigned int size)
{
    struct spi_ioc_transfer t;
    unsigned int n = size / sizeof(t), total = 0;

    if (size % sizeof(t))
        return -EINVAL;
    for (unsigned int i = 0; i < n; i++) {
        if (copy_from_user(&t, uarg + i * sizeof(t), sizeof(t)))
            return -EFAULT;
        total += t.len;
        if (total > SPI_BUFSIZ)
            return -EMSGSIZE;
        if (t.rx_buf && clear_user(lx_spi_ptr(t.rx_buf), t.len))
            return -EFAULT;
    }
    return total;
}

static long lx_spi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct lx_spi_file *f = file->private_data;
    void __user *uarg = compat_ptr(arg);
    u32 v32;
    u8 v8;

    if (_IOC_TYPE(cmd) != SPI_IOC_MAGIC)
        return -ENOTTY;
    if (_IOC_NR(cmd) == _IOC_NR(SPI_IOC_MESSAGE(1)) && _IOC_DIR(cmd) == _IOC_WRITE)
        return lx_spi_message(uarg, _IOC_SIZE(cmd));
    switch (cmd) {
    case SPI_IOC_RD_MODE:
        return put_user((u8)f->mode, (u8 __user *)uarg);
    case SPI_IOC_RD_MODE32:
        return put_user(f->mode, (u32 __user *)uarg);
    case SPI_IOC_RD_LSB_FIRST:
        return put_user((u8) !!(f->mode & SPI_LSB_FIRST), (u8 __user *)uarg);
    case SPI_IOC_RD_BITS_PER_WORD:
        return put_user(f->bits, (u8 __user *)uarg);
    case SPI_IOC_RD_MAX_SPEED_HZ:
        return put_user(f->speed, (u32 __user *)uarg);
    case SPI_IOC_WR_MODE:
    case SPI_IOC_WR_LSB_FIRST:
    case SPI_IOC_WR_BITS_PER_WORD:
        if (get_user(v8, (u8 __user *)uarg))
            return -EFAULT;
        if (cmd == SPI_IOC_WR_MODE)
            f->mode = (f->mode & ~0xffu) | v8;
        else if (cmd == SPI_IOC_WR_LSB_FIRST)
            f->mode = v8 ? f->mode | SPI_LSB_FIRST : f->mode & ~SPI_LSB_FIRST;
        else
            f->bits = v8 ? v8 : 8;
        return 0;
    case SPI_IOC_WR_MODE32:
    case SPI_IOC_WR_MAX_SPEED_HZ:
        if (get_user(v32, (u32 __user *)uarg))
            return -EFAULT;
        if (cmd == SPI_IOC_WR_MODE32)
            f->mode = v32;
        else
            f->speed = v32;
        return 0;
    }
    return -ENOTTY;
}

static const struct file_operations lx_spi_fops = {
    .owner = THIS_MODULE,
    .open = lx_spi_open,
    .release = lx_spi_release,
    .read = lx_spi_read,
    .write = lx_spi_write,
    .unlocked_ioctl = lx_spi_ioctl,
    .compat_ioctl = lx_spi_ioctl,
};

static struct lgemu_cdev spi_cdev;

void lx_spi_remove(void)
{
    lgemu_cdev_del(&spi_cdev);
}

int lx_spi_add(void)
{
    int err;

    if (!spi_cs)
        return 0;
    if (spi_cs > SPI_MAX_CS)
        return -EINVAL;
    err = lgemu_cdev_add(&spi_cdev, SPI_MAJOR, 0, spi_cs, "spidev", &lx_spi_fops);
    if (err)
        return err;
    pr_info("spi: spidev0.0-%u, no chip modelled\n", spi_cs - 1);
    return 0;
}
