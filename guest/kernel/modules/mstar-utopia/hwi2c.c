// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
#include "private.h"

/* ---- HWI2C (Utopia module 31): the board's I2C devices ----
 *
 * LG's MAdp_IIC_Read/Write (W16R's libkadaptor) turn the HAL's channel into an MStar port
 * (_IIC_CheckPhyPort: channel 1 port 4, 2 port 0, 3 port 1, 5 port 2, ...) and call
 * MDrv_HWI2C_ReadBytes_Customer / _WriteBytes_Customer with SlaveCfg = port << 8 | the 8-bit slave,
 * which pass their arguments to the kernel as commands 12 and 9: struct hwi2c_customer32 below,
 * with a pointer to the caller's MS_BOOL result, which it sets to TRUE before the call. A SlaveCfg
 * the parameter i2c places a device at is that lgemu.ko device, and the result says whether it
 * acknowledged. Any other slave does not acknowledge, as a board without that chip: a success
 * with the read buffer untouched made W16R's Si2178B driver (SlaveCfg 0x02c0, the tuner
 * ToolOpt4 selects, the only other slave its boot addresses) poll each command's CTS bit for its
 * full second, and tvservice's tv-ready came about 7 s later. */
#define HWI2C_MAX_ADDR     16
#define HWI2C_MAX_TRANSFER (32 * 1024)

static char *i2c[8];
static int i2c_count;
module_param_array(i2c, charp, &i2c_count, 0444);
MODULE_PARM_DESC(i2c, "the board's I2C devices, device:slavecfg (micom, nvm, amp; SlaveCfg = "
                      "port << 8 | the 8-bit slave)");

static struct {
    u16 slave_cfg;
    enum lgemu_i2c_device dev;
} hwi2c_map[ARRAY_SIZE(i2c)];

/* HWI2C's _Customer arguments from a 32-bit process (MDrv_HWI2C_ReadBytes_Customer builds them) */
struct hwi2c_customer32 {
    u16 slave_cfg;
    u16 pad;
    u32 addr_cnt; /* the register address's length */
    u32 reg_addr; /* the register address, a user pointer */
    u32 size;
    u32 data;   /* a user pointer */
    u32 port;   /* _IIC_CheckPhyPort's HWI2C_PORT */
    u32 mode;   /* the clock's read mode */
    u32 result; /* a user pointer to the MS_BOOL (u8) result */
};
static_assert(sizeof(struct hwi2c_customer32) == 32);

int hwi2c_parse(void)
{
    for (int i = 0; i < i2c_count; i++) {
        unsigned int cfg;

        if (lgemu_i2c_parse(i2c[i], &hwi2c_map[i].dev, &cfg, 1) || cfg > 0xffff) {
            pr_err("i2c=%s: not device:slavecfg\n", i2c[i]);
            return -EINVAL;
        }
        hwi2c_map[i].slave_cfg = cfg;
        pr_info("hwi2c: %s\n", i2c[i]);
    }
    return 0;
}

/* Commands 9 and 12: a device of the map, or no acknowledgement for a SlaveCfg without one. */
long hwi2c_customer(u32 cmd, u32 uarg)
{
    struct hwi2c_customer32 a;
    enum lgemu_i2c_device dev = LGEMU_I2C_NONE;
    u8 sub[HWI2C_MAX_ADDR], ok;
    int ret;

    if (copy_from_user(&a, compat_ptr(uarg), sizeof(a)))
        return -EFAULT;
    for (int i = 0; i < i2c_count; i++)
        if (hwi2c_map[i].slave_cfg == a.slave_cfg)
            dev = hwi2c_map[i].dev;
    if (dev == LGEMU_I2C_NONE) {
        ok = 0; /* no chip there: not acknowledged */
        return copy_to_user(compat_ptr(a.result), &ok, 1) ? -EFAULT : 0;
    }
    if (a.addr_cnt > HWI2C_MAX_ADDR || a.size > HWI2C_MAX_TRANSFER)
        return -EINVAL;
    /* On the wire, as MStar's driver sends it: the register address and the data in one write,
     * or the register address written and a read after a repeated start. */
    if (copy_from_user(sub, compat_ptr(a.reg_addr), a.addr_cnt))
        return -EFAULT;
    ret = lgemu_i2c_user_xfer(dev, (a.slave_cfg & 0xff) >> 1, sub, a.addr_cnt, compat_ptr(a.data),
                              a.size, cmd != MDRV_CMD_HWI2C_WRITEBYTES_CUST);
    if (ret == -EFAULT || ret == -ENOMEM)
        return ret;
    ok = ret >= 0;
    return copy_to_user(compat_ptr(a.result), &ok, 1) ? -EFAULT : 0;
}

/* ---- LM14's HWI2C master registers (lm14_hwi2c) ----
 *
 * LM14's MStar build drives HWI2C from user mode (libdrvHWI2C, which MAdp_IIC_Read/Write call for
 * the HAL's channels 0, 2, 4 and 5: ports 1, 18, 33 and 41), not through UTPA2's kernel commands
 * above. Its HAL addresses a port's registers as bytes from the non-PM RIU base: bank 0x11800 plus
 * the port's offset (HAL_HWI2C_SetPortRegOffset: 0, 0x100, 0x200, 0x300, 0x10400, 0x10500), a
 * byte at phys 0x1f200000 + 2 * (addr & ~1) + (addr & 1). Per port (byte addresses): 0x00 control
 * (bit 0 reset; HAL_HWI2C_Master_Enable sets 2, 3, 5, 6), 0x02 bit 0 START, 0x03 bit 0 STOP, 0x04
 * the byte to send (writing it sends it), 0x05 bit 0 the receiver's NACK of it, 0x06 the received
 * byte, 0x07 a receive (1, or 3: bit 1 a NACK after it, the last byte), 0x08 bit 0 the interrupt
 * flag every one of those raises (written 1 to clear), 0x0a bits 4:0 the state (0 idle); the
 * driver polls the flag after each operation and the state after STOP (HAL_HWI2C_Start, Stop,
 * Send_Byte, Recv_Byte). The rest (clock dividers, DMA) is register storage.
 *
 * The slaves are lgemu.ko's devices where i2c= places them (SlaveCfg = port index << 8 | the
 * 8-bit slave: micom:0x0052 is the micom on port 1). An address no device has is not
 * acknowledged. The bus carries what the devices take, whole messages: the bytes written after an
 * address go to the device at the STOP or the repeated START that ends them, a read is one
 * transfer when its address is sent (the write before it, then HWI2C_PREFETCH bytes the receives
 * then hand out in order), since a device here answers a message, not a clocked bit. Writes'
 * bytes are acknowledged as they are sent and the device's verdict comes with the transfer. A
 * receive outside a read and anything unknown raise no flag: they stay pending. */
#define HWI2C_PORTS    6
#define HWI2C_PREFETCH 32
static bool lm14_hwi2c;
module_param(lm14_hwi2c, bool, 0444);
MODULE_PARM_DESC(lm14_hwi2c,
                 "LM14 synchronous user-mode HWI2C master registers wired to the i2c= devices");
static const u64 hwi2c_port_pa[HWI2C_PORTS] = {
    0x1f223000, 0x1f223200, 0x1f223400, 0x1f223600, 0x1f243800, 0x1f243a00,
};
enum hwi2c_phase { HWI2C_IDLE, HWI2C_STARTED, HWI2C_WRITING, HWI2C_READING };
static struct hwi2c_port {
    enum hwi2c_phase phase;
    enum lgemu_i2c_device dev;
    u8 slave;                    /* 8-bit address of the current message */
    u8 regs[0x10];               /* byte addresses 0x00-0x0f */
    u8 out[HWI2C_MAX_ADDR + 64]; /* the write being collected */
    unsigned int out_len;
    u8 in[HWI2C_PREFETCH]; /* the read's bytes */
    unsigned int in_pos;
    u32 transfers, nacks;
} hwi2c_ports[HWI2C_PORTS];
static DEFINE_MUTEX(hwi2c_lock);

static enum lgemu_i2c_device hwi2c_device(unsigned int port, u8 slave)
{
    for (int i = 0; i < i2c_count; i++)
        if (hwi2c_map[i].slave_cfg == (port << 8 | (slave & 0xfe)))
            return hwi2c_map[i].dev;
    return LGEMU_I2C_NONE;
}

static void hwi2c_done(struct hwi2c_port *p, unsigned int state)
{
    p->regs[0x0a] = state;
    p->regs[0x08] |= 1;
}

/* the collected write, as the message the device gets at its STOP or repeated START */
static void hwi2c_flush_write(struct hwi2c_port *p)
{
    struct i2c_msg msg = {.addr = p->slave >> 1, .len = p->out_len, .buf = p->out};

    if (p->phase != HWI2C_WRITING || p->dev == LGEMU_I2C_NONE)
        return;
    p->transfers++;
    if (lgemu_i2c_xfer(p->dev, &msg, 1) < 0)
        p->nacks++;
    p->out_len = 0;
}

static void hwi2c_send(struct hwi2c_port *p, unsigned int port, u8 byte)
{
    bool nack = false;

    if (p->phase == HWI2C_STARTED) {
        bool pending;

        /* a write a repeated start ended goes now, unless this reads it back from its slave */
        if (p->out_len && !((byte & 1) && (byte & 0xfe) == p->slave)) {
            p->phase = HWI2C_WRITING;
            hwi2c_flush_write(p);
        }
        pending = p->out_len != 0;
        p->dev = hwi2c_device(port, byte);
        if (p->dev == LGEMU_I2C_NONE) {
            nack = true;
            p->phase = HWI2C_IDLE;
        } else if (byte & 1) {
            /* the read, after the write it repeats a start from (if any) */
            struct i2c_msg msgs[2] = {
                {.addr = byte >> 1, .len = p->out_len, .buf = p->out},
                {.addr = byte >> 1, .flags = I2C_M_RD, .len = sizeof(p->in), .buf = p->in},
            };
            int first = pending ? 0 : 1;

            memset(p->in, 0xff, sizeof(p->in));
            p->transfers++;
            if (lgemu_i2c_xfer(p->dev, msgs + first, 2 - first) < 0) {
                p->nacks++;
                nack = true;
                p->phase = HWI2C_IDLE;
            } else {
                p->phase = HWI2C_READING;
                p->in_pos = 0;
            }
            p->out_len = 0;
        } else {
            p->phase = HWI2C_WRITING;
            p->out_len = 0;
        }
        p->slave = byte & 0xfe;
    } else if (p->phase == HWI2C_WRITING && p->out_len < sizeof(p->out)) {
        p->out[p->out_len++] = byte;
    } else {
        nack = true;
    }
    p->regs[0x05] = (p->regs[0x05] & ~1) | nack;
    hwi2c_done(p, 3);
}

static int hwi2c_write_byte(unsigned int port, unsigned int reg, u8 v)
{
    struct hwi2c_port *p = &hwi2c_ports[port];

    switch (reg) {
    case 0x00:
        p->regs[0] = v;
        if (v & 1) { /* reset: the bus idle, nothing pending */
            p->phase = HWI2C_IDLE;
            p->out_len = 0;
            p->regs[0x08] = p->regs[0x0a] = 0;
        }
        return 0;
    case 0x02:
        if (v & 1) {
            if (p->phase == HWI2C_WRITING && p->out_len) {
                /* a repeated start: the write waits to see whether a read of it follows */
            } else {
                p->out_len = 0;
            }
            p->phase = HWI2C_STARTED;
            hwi2c_done(p, 1);
        }
        return 0;
    case 0x03:
        if (v & 1) {
            if (p->phase == HWI2C_STARTED && p->out_len)
                p->phase = HWI2C_WRITING; /* a STOP after a repeated start's address */
            hwi2c_flush_write(p);
            p->phase = HWI2C_IDLE;
            p->out_len = 0;
            hwi2c_done(p, 0);
        }
        return 0;
    case 0x04:
        p->regs[0x04] = v;
        hwi2c_send(p, port, v);
        return 0;
    case 0x05:
    case 0x06:
    case 0x0a:
    case 0x0b:
        return 0; /* the controller's own */
    case 0x07:
        p->regs[0x07] = v;
        if (p->phase != HWI2C_READING || (v & ~3) || !(v & 1))
            return 0; /* no read in progress: no completion */
        p->regs[0x06] = p->in_pos < sizeof(p->in) ? p->in[p->in_pos++] : 0xff;
        hwi2c_done(p, 7);
        return 0;
    case 0x08:
        p->regs[0x08] &= ~(v & 1); /* written 1 to clear */
        return 0;
    default:
        p->regs[reg] = v;
        return 0;
    }
}

/* Caller: lm14_mmio_access, for the two pages of the six ports. */
int lm14_hwi2c_access(u64 addr, unsigned int width, bool write, u32 *value)
{
    u8 *page = lgemu_store_ptr(mmio_store(), addr & PAGE_MASK);
    int port = -1;
    unsigned int off = 0;

    for (int i = 0; i < HWI2C_PORTS; i++)
        if (addr >= hwi2c_port_pa[i] && addr + width <= hwi2c_port_pa[i] + 0x20) {
            port = i;
            off = addr - hwi2c_port_pa[i];
        }
    if (!page || (width != 1 && width != 2 && width != 4))
        return -EINVAL;
    mutex_lock(&hwi2c_lock);
    if (port < 0) {
        /* outside the decoded registers: storage */
        if (write)
            memcpy(page + offset_in_page(addr), value, width);
        else {
            *value = 0;
            memcpy(value, page + offset_in_page(addr), width);
        }
        mutex_unlock(&hwi2c_lock);
        return 0;
    }
    if (!write)
        *value = 0;
    for (unsigned int i = 0; i < width; i++) {
        unsigned int lane = off + i;
        unsigned int reg = (lane >> 2) * 2 + (lane & 1);

        if (lane & 2) { /* the upper half of a 32-bit slot: not a register */
            if (write) {
                mutex_unlock(&hwi2c_lock);
                return -EINVAL;
            }
            continue;
        }
        if (write)
            hwi2c_write_byte(port, reg, *value >> (8 * i));
        else
            *value |= (u32)hwi2c_ports[port].regs[reg] << (8 * i);
    }
    mutex_unlock(&hwi2c_lock);
    return 0;
}

static ssize_t hwi2c_state_read(struct file *file, char __user *buf, size_t n, loff_t *pos)
{
    char state[512];
    int len = 0;

    mutex_lock(&hwi2c_lock);
    for (int i = 0; i < HWI2C_PORTS; i++)
        len +=
            scnprintf(state + len, sizeof(state) - len,
                      "port %d: phase %d ctrl %#04x int %u nack %u transfers %u nacks %u\n", i,
                      hwi2c_ports[i].phase, hwi2c_ports[i].regs[0], hwi2c_ports[i].regs[8] & 1,
                      hwi2c_ports[i].regs[5] & 1, hwi2c_ports[i].transfers, hwi2c_ports[i].nacks);
    mutex_unlock(&hwi2c_lock);
    return simple_read_from_buffer(buf, n, pos, state, len);
}
const struct proc_ops hwi2c_state_proc_ops = {
    .proc_read = hwi2c_state_read,
    .proc_lseek = default_llseek,
};

bool hwi2c_enabled(void)
{
    return lm14_hwi2c;
}
