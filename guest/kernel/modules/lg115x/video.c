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

/* ---- the display engine: /dev/lg/de0 ---- */

#define DE_MAJOR       145
#define DE_MINORS      4 /* de0-de3 in the device_table; the stock driver serves 2 */
#define DE_IOC_MAGIC   'a'
#define DE_WINS        2 /* windows with a VTM slot pair */
#define DE_VTM_BUFFERS 3 /* MAX_VTG_BUFFER */
#define DE_SEEN_MAX    64

/* LX_DE_RECT_T */
struct lx_de_rect {
    u16 w, h, x, y;
};

/* LX_DE_CAPACITY_T, 80 bytes */
struct lx_de_capacity {
    u32 chip_id, max_win, max_layer;
    struct lx_de_rect max_in_size, max_out_size;
    u32 max_in_fr_rate, max_out_fr_rate, mvir_start_num, default_3d_out_fmt;
    struct lx_de_rect vtm_max; /* vtm_capa: LX_DE_VT_CAPA_PARAM_T */
    u8 left_top_align, input_deint, display_deint, scale_up;
    u32 up_w, up_h;
    u8 scale_down;
    u32 down_w, down_h, location_limit;
};
static_assert(sizeof(struct lx_de_capacity) == 80);

/* LX_DE_SYS_STATUS_T, 28 bytes */
struct lx_de_sys_status {
    u32 status_type, display_type, display_mirror, frc_type, trid_type, fc_mem, display_size;
};

/* LX_DE_FR_RATE_T, 8 bytes */
struct lx_de_fr_rate {
    u32 force_free_run;
    u16 fr_rate;
};

/* LX_DE_VTM_FRAME_INFO_T with _USE_VT_EXTENTION_, 112 bytes: the VTM's shared-memory slot */
struct lx_de_vtm {
    u32 win_id, address, enable, rate;
    u8 frz, secure, location, reserved;
    u32 stride, sampling;
    u32 y_frame[DE_VTM_BUFFERS], c_frame[DE_VTM_BUFFERS];
    u32 r_idx, mute;
    struct lx_de_rect src_size, win_size;
    u16 mirror, scan;
    s32 mem_fd;
    /* ext_info: LX_DE_VT_EXTENTION_INFO_T */
    u32 ext_location;
    struct lx_de_rect crop_size, w_out_size;
    u32 ext_rate;
    u8 ext_frz, ext_secure, ext_reserved[2];
};
static_assert(sizeof(struct lx_de_vtm) == 112);

/* LX_DE_SOURCE_SIZE_WIN_T, 20 bytes */
struct lx_de_src_size {
    u32 win_id;
    struct lx_de_rect in_size, in_win;
};

/* LX_DE_VTM_BLOCK_STATE_T */
struct lx_de_vtm_block {
    u32 win_id, cmd, block, reserved;
};

#define DE_IOR_GET_CAPACITY         _IOR(DE_IOC_MAGIC, 4, struct lx_de_capacity)
#define DE_IOR_GET_OUT_FR_RATE      _IOR(DE_IOC_MAGIC, 8, struct lx_de_fr_rate)
#define DE_IOR_GET_SYS_STATUS       _IOR(DE_IOC_MAGIC, 10, struct lx_de_sys_status)
#define DE_IOR_GET_SRC_STATUS       _IOC(_IOC_READ, DE_IOC_MAGIC, 26, 32)
#define DE_IOW_SET_VTM              _IOW(DE_IOC_MAGIC, 88, struct lx_de_vtm)
#define DE_IOR_GET_VTM              _IOR(DE_IOC_MAGIC, 89, struct lx_de_vtm)
#define DE_IOW_GET_VTM_BUFFER       _IOW(DE_IOC_MAGIC, 90, struct lx_de_vtm)
#define DE_IOWR_SET_VTM_BLOCK_STATE _IOWR(DE_IOC_MAGIC, 92, struct lx_de_vtm_block)
#define DE_IOW_SET_SUB_MODE         _IOW(DE_IOC_MAGIC, 52, u32)
#define DE_IOW_GET_SUB_MODE         _IOR(DE_IOC_MAGIC, 53, u32) /* sic: an _IOR */
#define DE_IOW_SET_SRC_SIZE         _IOW(DE_IOC_MAGIC, 55, struct lx_de_src_size)
#define DE_IOR_GET_SRC_SIZE         _IOR(DE_IOC_MAGIC, 56, struct lx_de_src_size)

static char *de_mem, *de_capacity, *de_abi;
static unsigned int de_panel;
module_param(de_mem, charp, 0444);
MODULE_PARM_DESC(de_mem, "the display engine's memory, base:size: the VTM page at base, the "
                         "captured frames' addresses after it; unset: no /dev/lg/de0");
module_param(de_capacity, charp, 0444);
MODULE_PARM_DESC(
    de_capacity,
    "LX_DE_CAPACITY_T's 23 values, in order (chip_id:max_win:max_layer:"
    "in w:h:out w:h:in_fr:out_fr:mvir:3d_fmt:vtm w:h:4 flags:up w:h:down:down w:h:location)");
module_param(de_abi, charp, 0444);
MODULE_PARM_DESC(de_abi, "the DE's ioctl numbers set_vtm:get_vtm:vtm_block:sys_status:src_status:"
                         "out_fr_rate (M14 49:50:55:86:87:22); unset: M16P's (88:89:92:10:26:8), "
                         "with its sub-window mode, source sizes and GET_VTM_BUFFER");
module_param(de_panel, uint, 0444);
MODULE_PARM_DESC(de_panel, "LX_DE_PANEL_TYPE_T, the system status's display size (0 1920, 4 3840)");

static struct lx_de_capacity de_capa;
static u32 de_base, de_size, de_frame_size;
static struct lx_de_vtm *de_vtm; /* the VTM page: per window, the request and the reply */
static u32 de_blocked[4];        /* SET_VTM_BLOCK_STATE, per window */
/* what resource_mgr.c keeps and answers: the sub-window mode, each window's source size */
static u32 de_sub_mode;
static struct lx_de_src_size de_src_size[4];
static DEFINE_MUTEX(de_lock);
static u32 de_seen[DE_SEEN_MAX];
static unsigned int de_seen_count;

/* The commands whose numbers move between chips (de_abi), as M16P numbers them */
static const unsigned int de_moved[] = {
    DE_IOW_SET_VTM,        DE_IOR_GET_VTM,        DE_IOWR_SET_VTM_BLOCK_STATE,
    DE_IOR_GET_SYS_STATUS, DE_IOR_GET_SRC_STATUS, DE_IOR_GET_OUT_FR_RATE};
static u32 de_nr[ARRAY_SIZE(de_moved)];
static bool de_abi_given;

/* cmd as M16P numbers it: a moved command at the chip's number, or M16P's own commands without
 * de_abi; 0 for anything else, whose number means something else on the chip */
static unsigned int de_command(unsigned int cmd)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(de_moved); i++)
        if (cmd == ((de_moved[i] & ~_IOC_NRMASK) | de_nr[i] << _IOC_NRSHIFT))
            return de_moved[i];
    switch (cmd) {
    case DE_IOR_GET_CAPACITY:
        return cmd;
    case DE_IOW_GET_VTM_BUFFER:
    case DE_IOW_SET_SUB_MODE:
    case DE_IOW_GET_SUB_MODE:
    case DE_IOW_SET_SRC_SIZE:
    case DE_IOR_GET_SRC_SIZE:
        return de_abi_given ? 0 : cmd;
    }
    return 0;
}

/* a frame buffer's planes: the luma at the buffer, the chroma (4:2:0, interleaved) after it */
static u32 de_y_frame(unsigned int i)
{
    return de_base + PAGE_SIZE + i * de_frame_size;
}

static u32 de_c_frame(unsigned int i)
{
    return de_y_frame(i) + de_frame_size * 2 / 3;
}

/* DE_IO_SetVTM: the stock driver keeps a copy of the request in its firmware's shared memory
 * ("webOS"), next to the one the DE firmware answers in (the "MCU" copy, which it waits up to
 * 1 s to fill), and returns the page's address, which KADP maps (448 bytes: two windows' pairs;
 * on M17, chip_id > 4, KADP reads window w's state from slot 2w + 1, the reply). M14's KADP
 * (chip_id 2) maps the page through /dev/mem and reads window w's state from slot w, one 112-byte
 * record a window (KADP_DE_SetVTM, KADP_DE_CmdVTM): the reply alone, no request copy. Here the
 * reply is written at once: the frame buffers' addresses (in de_mem, which nothing backs: tvservice
 * maps them through its own /dev/mem, guest/layers/common/tvservice), a 4:2:0 semi-planar frame at
 * the requested output size, read index 0, no mute. */
static long de_set_vtm(void __user *uarg)
{
    struct lx_de_vtm r, *req, *reply;
    u16 w, h;

    if (copy_from_user(&r, uarg, sizeof(r)))
        return -EFAULT;
    if (r.win_id >= DE_WINS)
        return -EINVAL;
    if (de_capa.chip_id > 4) {
        req = &de_vtm[2 * r.win_id];
        reply = req + 1;
    } else {
        req = NULL;
        reply = &de_vtm[r.win_id];
    }
    if (r.enable) {
        w = clamp_t(u16, r.w_out_size.w ?: de_capa.vtm_max.w, de_capa.down_w, de_capa.vtm_max.w);
        h = clamp_t(u16, r.w_out_size.h ?: de_capa.vtm_max.h, de_capa.down_h, de_capa.vtm_max.h);
        if (req) {
            *req = r;
            req->secure = 0xff;
            req->y_frame[0] = de_y_frame(0);
        }
        *reply = r;
        reply->address = de_base;
        reply->stride = ALIGN(w, 16);
        reply->sampling = 0; /* LX_DE_SCALER_CSAMPLE_T 4:2:0 */
        for (unsigned int i = 0; i < DE_VTM_BUFFERS; i++) {
            reply->y_frame[i] = de_y_frame(i);
            reply->c_frame[i] = de_c_frame(i);
        }
        reply->r_idx = 0;
        reply->mute = 0;
        reply->secure = 0;
        reply->w_out_size = (struct lx_de_rect){.w = w, .h = h};
        reply->win_size = reply->w_out_size;
        /* M14's KADP_DE_GetVTMStatus gives a panel of 1920 or less the frame's size from
         * src_size (HAL_VT_GetAllVideoFrameBufferProperty's width and height) */
        if (!req)
            reply->src_size = reply->w_out_size;
        reply->ext_secure = 0;
        reply->mem_fd = 0;
        r.y_frame[0] = de_y_frame(0);
    } else {
        if (req)
            req->enable = 0;
        reply->enable = 0;
    }
    r.address = de_base;
    return copy_to_user(uarg, &r, sizeof(r)) ? -EFAULT : 0;
}

static long de_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    void __user *uarg = compat_ptr(arg);
    struct lx_de_sys_status sys = {.display_size = de_panel};
    struct lx_de_fr_rate fr = {.fr_rate = de_capa.max_out_fr_rate};
    struct lx_de_vtm_block blk;
    struct lx_de_vtm vtm = {0};
    long ret;
    unsigned int i;

    mutex_lock(&de_lock);
    switch (de_command(cmd)) {
    case DE_IOR_GET_CAPACITY:
        ret = copy_to_user(uarg, &de_capa, sizeof(de_capa)) ? -EFAULT : 0;
        break;
    case DE_IOR_GET_SYS_STATUS: /* LCD, no mirror, internal FRC, 3D off */
        ret = copy_to_user(uarg, &sys, sizeof(sys)) ? -EFAULT : 0;
        break;
    case DE_IOR_GET_OUT_FR_RATE:
        ret = copy_to_user(uarg, &fr, sizeof(fr)) ? -EFAULT : 0;
        break;
    case DE_IOR_GET_SRC_STATUS: /* no input source on either window */
        ret = clear_user(uarg, _IOC_SIZE(cmd)) ? -EFAULT : 0;
        break;
    case DE_IOW_SET_VTM:
        ret = de_set_vtm(uarg);
        break;
    case DE_IOR_GET_VTM: /* DE_HAL_GetVTM: the frame memory's address */
    case DE_IOW_GET_VTM_BUFFER:
        vtm.address = de_y_frame(0);
        ret = copy_to_user(uarg, &vtm, sizeof(vtm)) ? -EFAULT : 0;
        break;
    case DE_IOW_SET_SUB_MODE:
        ret = get_user(de_sub_mode, (u32 __user *)uarg);
        break;
    case DE_IOW_GET_SUB_MODE:
        ret = put_user(de_sub_mode, (u32 __user *)uarg);
        break;
    case DE_IOW_SET_SRC_SIZE:
    case DE_IOR_GET_SRC_SIZE:
        ret = get_user(i, (u32 __user *)uarg);
        if (!ret && i >= ARRAY_SIZE(de_src_size))
            ret = -EINVAL;
        if (ret)
            break;
        de_src_size[i].win_id = i;
        if (cmd == DE_IOW_SET_SRC_SIZE)
            ret = copy_from_user(&de_src_size[i], uarg, sizeof(de_src_size[i])) ? -EFAULT : 0;
        else
            ret = copy_to_user(uarg, &de_src_size[i], sizeof(de_src_size[i])) ? -EFAULT : 0;
        break;
    case DE_IOWR_SET_VTM_BLOCK_STATE: /* DE_IO_SetVTMBlockState: 0 get, 1 set */
        ret = copy_from_user(&blk, uarg, sizeof(blk)) ? -EFAULT : 0;
        if (!ret && blk.win_id >= ARRAY_SIZE(de_blocked))
            ret = -EINVAL;
        if (!ret) {
            if (blk.cmd == 0)
                blk.block = de_blocked[blk.win_id];
            else if (blk.cmd == 1)
                de_blocked[blk.win_id] = blk.block;
            ret = copy_to_user(uarg, &blk, sizeof(blk)) ? -EFAULT : 0;
        }
        break;
    default:
        /* The rest of the display engine (windows, inputs, the firmware, PQ paths): a setting is
         * accepted and changes nothing; a query is not answered. Each command is logged once. */
        ret = _IOC_TYPE(cmd) == DE_IOC_MAGIC && _IOC_DIR(cmd) == _IOC_WRITE ? 0 : -ENOTTY;
        for (i = 0; i < de_seen_count && de_seen[i] != cmd; i++)
            ;
        if (i == de_seen_count && i < DE_SEEN_MAX) {
            de_seen[de_seen_count++] = cmd;
            pr_info("de: ioctl %#x (nr %u, %u bytes) %s (%s)\n", cmd, _IOC_NR(cmd), _IOC_SIZE(cmd),
                    ret ? "not answered" : "accepted", current->comm);
        }
    }
    mutex_unlock(&de_lock);
    return ret;
}

/* KADP maps the VTM page at the address SET_VTM returned (the stock driver's DE_Mmap takes the
 * firmware windows of de_cfg_get_fcmem): read and written, KADP_DE_VTMFrameControl writes the
 * freeze and rate into the reply. */
static int de_mmap(struct file *file, struct vm_area_struct *vma)
{
    if (vma->vm_pgoff != de_base >> PAGE_SHIFT || vma->vm_end - vma->vm_start > PAGE_SIZE) {
        pr_info("de: mmap of %#lx+%#lx is not the VTM page (%s)\n", vma->vm_pgoff << PAGE_SHIFT,
                vma->vm_end - vma->vm_start, current->comm);
        return -EINVAL;
    }
    return remap_vmalloc_range(vma, de_vtm, 0);
}

static int de_open(struct inode *inode, struct file *file)
{
    return 0;
}

static const struct file_operations de_fops = {
    .owner = THIS_MODULE,
    .open = de_open,
    .unlocked_ioctl = de_ioctl,
    .compat_ioctl = de_ioctl,
    .mmap = de_mmap,
};

static struct lgemu_cdev de_cdev;

void lx_de_remove(void)
{
    if (!de_cdev.added)
        return;
    lgemu_cdev_del(&de_cdev);
    vfree(de_vtm);
}

int lx_de_add(void)
{
    u32 mem[2], c[23];
    int err;

    if (!de_mem)
        return 0;
    if (lgemu_parse_u32s(de_mem, 16, mem, 2) || (mem[0] | mem[1]) & ~PAGE_MASK || !de_capacity ||
        lgemu_parse_u32s(de_capacity, 0, c, ARRAY_SIZE(c)) || !c[1] || !c[11] || !c[12]) {
        pr_err("de: de_mem is base:size, page-aligned, and de_capacity 23 numbers\n");
        return -EINVAL;
    }
    de_capa = (struct lx_de_capacity){
        .chip_id = c[0],
        .max_win = c[1],
        .max_layer = c[2],
        .max_in_size = {.w = c[3], .h = c[4]},
        .max_out_size = {.w = c[5], .h = c[6]},
        .max_in_fr_rate = c[7],
        .max_out_fr_rate = c[8],
        .mvir_start_num = c[9],
        .default_3d_out_fmt = c[10],
        .vtm_max = {.w = c[11], .h = c[12]},
        .left_top_align = c[13],
        .input_deint = c[14],
        .display_deint = c[15],
        .scale_up = c[16],
        .up_w = c[17],
        .up_h = c[18],
        .scale_down = c[19],
        .down_w = c[20],
        .down_h = c[21],
        .location_limit = c[22],
    };
    for (unsigned int i = 0; i < ARRAY_SIZE(de_moved); i++)
        de_nr[i] = _IOC_NR(de_moved[i]);
    if (de_abi) {
        if (lgemu_parse_u32s(de_abi, 0, de_nr, ARRAY_SIZE(de_nr))) {
            pr_err("de: de_abi is six ioctl numbers\n");
            return -EINVAL;
        }
        for (unsigned int i = 0; i < ARRAY_SIZE(de_nr); i++)
            if (de_nr[i] > _IOC_NRMASK) {
                pr_err("de: de_abi: %u is no ioctl number\n", de_nr[i]);
                return -EINVAL;
            }
        de_abi_given = true;
    }
    de_base = mem[0];
    de_size = mem[1];
    /* a frame at the VT's largest size, luma and 4:2:0 chroma, rounded to whole pages */
    de_frame_size = PAGE_ALIGN(ALIGN(c[11], 16) * c[12] * 3 / 2);
    de_frame_size = ALIGN(de_frame_size, 3 * PAGE_SIZE); /* the chroma at 2/3, page-aligned */
    if (PAGE_SIZE + DE_VTM_BUFFERS * (u64)de_frame_size > de_size) {
        pr_err("de: de_mem's %#x bytes hold no %u frames of %ux%u\n", de_size, DE_VTM_BUFFERS,
               c[11], c[12]);
        return -EINVAL;
    }
    de_vtm = vmalloc_user(PAGE_SIZE);
    if (!de_vtm)
        return -ENOMEM;
    err = lgemu_cdev_add(&de_cdev, DE_MAJOR, 0, DE_MINORS, "de", &de_fops);
    if (err) {
        vfree(de_vtm);
        return err;
    }
    pr_info("de: de0-%u, chip %u, VTM page %#x, %u frames of %ux%u from %#x\n", DE_MINORS - 1,
            de_capa.chip_id, de_base, DE_VTM_BUFFERS, c[11], c[12], de_y_frame(0));
    return 0;
}

/* ---- picture enhancement: /dev/lg/pe0 ---- */

#define PE_MAJOR    156
#define PE_IOWR_PKT _IOWR('a', 0, struct lx_pe_pkt32) /* the whole API, 0xc00c6100 */
/* LX_PE_PKT_T's info: rwtype << 24 | modltype << 16 | functype */
#define PE_PKT_INIT         0
#define PE_PKT_SET          1
#define PE_PKT_GET          2
#define PE_MODL_NUM         15 /* INIT, DEFAULT, DBG, CMN, ... HDR */
#define PE_MODL_ETC         12
#define PE_FUNC_INF_LEVEL   0x0c02 /* LX_PE_INF_LEVEL_T, LX_PE_MISC_START_CODE + 2 */
#define PE_MODL_HDR         14
#define PE_FUNC_HDR_INFO    0x0e05 /* LX_PE_HDR_INFO_T, LX_PE_HDR_START_CODE + 5 */
#define PE_FUNC_HDR_3D_INFO 0x0e06 /* LX_PE_HDR_3D_LUT_INFO_T, the same layout */
#define PE_LEVEL_MOTION     0x1    /* LX_PE_LEVEL_TYPE */
#define PE_LEVEL_NOISE      0x2
#define PE_LEVEL_PEAKPOS    0x4
#define PE_WIN_ALL          4 /* LX_PE_WIN_ALL (PE_EXTEND_WINID: windows 0-3) */
#define PE_DATA_MAX         (64 * 1024)
#define PE_SEEN_MAX         128

struct lx_pe_pkt32 {
    u32 info, size, data; /* data: a user pointer */
};

/* LX_PE_INF_LEVEL_T, the same in the 2014 and 2016 kdrivers */
struct lx_pe_level {
    u32 win_id, sel, noise, peakpos, motion;
};

/* LX_PE_HDR_INFO_T from a 32-bit process */
struct lx_pe_hdr_info32 {
    u32 win_id, oper, size, data; /* size: [15:0] the words at data, a user pointer */
};

static unsigned int pe_hdr;
module_param(pe_hdr, uint, 0444);
MODULE_PARM_DESC(pe_hdr,
                 "M17's HDR LUT map in DDR (PE_HDR_DDR_M17_BASE_ADDR: the BE's IPC firmware "
                 "region + 2 MiB), which PE_HDR_GetInfo describes; 0: no such map (H13, M14)");

/* pe_hdr_ddr_m17.h, version 3: the header (8 words), 12 3D LUTs (SDR, HDR, HLG and SDR-to-HDR,
 * three each), 4 EOTF, 4 OETF, 4 tone-map LUTs and the 4 EOTF LUTs version 3 adds, back to back
 * from the map's base: each item's base and size */
#define PE_HDR_VERSION      3
#define PE_HDR_HEADER_WORDS 8
#define PE_HDR_ITEMS        29
#define PE_HDR_MAP_SIZE     0x100000 /* to the end of the BE's IPC region (3 MiB from 2 MiB before) */
static u32 pe_hdr_item[PE_HDR_ITEMS][2];

static void pe_hdr_layout(void)
{
    static const struct {
        u32 count, size;
    } items[] = {
        {1, 0x20}, {12, 0x6660}, {4, 0x1000}, {4, 0x1000}, {4, 0x18c}, {4, 0x1000},
    };
    u32 base = pe_hdr;
    unsigned int n = 0;

    for (unsigned int i = 0; i < ARRAY_SIZE(items); i++)
        for (unsigned int j = 0; j < items[i].count; j++, n++) {
            pe_hdr_item[n][0] = base;
            pe_hdr_item[n][1] = items[i].size;
            base += items[i].size;
        }
}

static struct lgemu_store pe_store = LGEMU_STORE_INIT(pe_store);
static DEFINE_MUTEX(pe_lock);
static u32 pe_seen[PE_SEEN_MAX];
static unsigned int pe_seen_count;

/* PE_ETC_GetLevelInfo: the TNR's status registers (motion, noise and peak position levels; M17
 * reads the motion level only), which read 0 with no picture going through the TNR. */
static long pe_get_level(void __user *data, u32 size)
{
    struct lx_pe_level l;

    if (size != sizeof(l))
        return -EPERM; /* the stock's "size mismatch" */
    if (copy_from_user(&l, data, sizeof(l)))
        return -EFAULT;
    if (l.win_id > PE_WIN_ALL)
        return -EPERM;
    if (l.sel & PE_LEVEL_MOTION)
        l.motion = 0;
    if (l.sel & PE_LEVEL_NOISE)
        l.noise = 0;
    if (l.sel & PE_LEVEL_PEAKPOS)
        l.peakpos = 0;
    return copy_to_user(data, &l, sizeof(l)) ? -EFAULT : 0;
}

/* PE_HDR_GET_TBLA: the request (window, operation, size in words, a buffer), the answer in the
 * buffer and the new size. PE_HDR_HW_M17_GetInfo: the HDR map's header (its first 8 words, which
 * the driver's init sets to the version and zeros) and each LUT's base and size; KADP_PE_InitHdr
 * maps it through this device and writes its LUTs there. PE_HDR_HW_M17_Get3dLutInfo: the
 * connection's three 3D LUTs as offsets into the map (W17H's kernel has constants there, 0x26660
 * for HLG1, not the GPL source's base + offset: KADP adds them to its mapping of the map's base),
 * the one in use from the map's header (word 0, which KADP writes), the connection SDR unless the
 * operation names one (no HDR source is ever connected here). Window 0 only, as the stock. */
static long pe_get_hdr(void __user *data, u32 size, u32 func)
{
    struct lx_pe_hdr_info32 h;
    unsigned int bufsize, words, i;
    u32 *buf, mode, *header;
    long ret = 0;

    if (size != sizeof(h))
        return -EPERM;
    if (copy_from_user(&h, data, sizeof(h)))
        return -EFAULT;
    bufsize = (h.size & 0xffff) * sizeof(u32);
    words = func == PE_FUNC_HDR_INFO ? PE_HDR_HEADER_WORDS + 2 * PE_HDR_ITEMS : 10;
    if (h.win_id != 0 || !h.data || bufsize < words * sizeof(u32))
        return -EPERM; /* "is short", or a window whose map is not kept */
    buf = kzalloc(bufsize, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;
    header = lgemu_store_ptr(&pe_store, pe_hdr);
    if (!header) {
        kfree(buf);
        return -ENOMEM;
    }
    if (func == PE_FUNC_HDR_INFO) {
        for (i = 0; i < PE_HDR_HEADER_WORDS; i++)
            buf[i] = READ_ONCE(header[i]);
        for (i = 0; i < PE_HDR_ITEMS; i++) {
            buf[PE_HDR_HEADER_WORDS + 2 * i] = pe_hdr_item[i][0];
            buf[PE_HDR_HEADER_WORDS + 2 * i + 1] = pe_hdr_item[i][1];
        }
        h.size = words;
    } else {
        mode = (h.oper & 0xf) >= 0xa && (h.oper & 0xf) <= 0xd ? (h.oper & 0xf) - 0xa : 0;
        buf[0] = READ_ONCE(*header) >> (16 + 4 * mode) & 0xf;
        buf[1] = mode;
        for (i = 0; i < 3; i++) {
            buf[2 + 2 * i] = pe_hdr_item[1 + 3 * mode + i][0] - pe_hdr;
            buf[3 + 2 * i] = pe_hdr_item[1 + 3 * mode + i][1];
        }
        h.size = 8;
    }
    if (copy_to_user(compat_ptr(h.data), buf, bufsize) || copy_to_user(data, &h, sizeof(h)))
        ret = -EFAULT;
    kfree(buf);
    return ret;
}

/* PE_DRV_ProcessPkt: an init or a setting (a table, a register set) is accepted and changes no
 * picture here; a query is answered for the level poll, refused otherwise. Each packet type is
 * logged once. */
static long pe_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct lx_pe_pkt32 pkt;
    u32 rw, modl, func;
    unsigned int i;
    long ret;

    if (cmd != PE_IOWR_PKT)
        return -ENOTTY;
    if (copy_from_user(&pkt, compat_ptr(arg), sizeof(pkt)))
        return -EFAULT;
    if (!pkt.size || !pkt.data || pkt.size > PE_DATA_MAX)
        return -EPERM;
    rw = pkt.info >> 24;
    modl = pkt.info >> 16 & 0xff;
    func = pkt.info & 0xffff;
    if (rw == PE_PKT_GET && modl == PE_MODL_ETC && func == PE_FUNC_INF_LEVEL)
        return pe_get_level(compat_ptr(pkt.data), pkt.size);
    if (rw == PE_PKT_GET && modl == PE_MODL_HDR && pe_hdr &&
        (func == PE_FUNC_HDR_INFO || func == PE_FUNC_HDR_3D_INFO))
        return pe_get_hdr(compat_ptr(pkt.data), pkt.size, func);
    ret = rw == PE_PKT_INIT || (rw == PE_PKT_SET && modl < PE_MODL_NUM) ? 0 : -EPERM;
    mutex_lock(&pe_lock);
    for (i = 0; i < pe_seen_count && pe_seen[i] != pkt.info; i++)
        ;
    if (i == pe_seen_count && i < PE_SEEN_MAX) {
        pe_seen[pe_seen_count++] = pkt.info;
        pr_info("pe: packet %#010x (rw %u, module %u, function %#x, %u bytes) %s (%s)\n", pkt.info,
                rw, modl, func, pkt.size, ret ? "not answered" : "accepted", current->comm);
    }
    mutex_unlock(&pe_lock);
    return ret;
}

static int pe_open(struct inode *inode, struct file *file)
{
    return 0;
}

/* KADP_PE_InitHdr maps the HDR map through the PE device, at its physical address: zeroed pages of
 * the module's, the same for every mapping (nothing reads the LUTs written there) */
static int pe_mmap(struct file *file, struct vm_area_struct *vma)
{
    u64 addr = (u64)vma->vm_pgoff << PAGE_SHIFT;

    if (!pe_hdr || addr < pe_hdr ||
        addr + (vma->vm_end - vma->vm_start) > (u64)pe_hdr + PE_HDR_MAP_SIZE) {
        pr_info("pe: mmap of %#llx+%#lx is not the HDR map (%s)\n", addr,
                vma->vm_end - vma->vm_start, current->comm);
        return -EINVAL;
    }
    return lgemu_store_mmap(&pe_store, vma, addr);
}

static const struct file_operations pe_fops = {
    .owner = THIS_MODULE,
    .open = pe_open,
    .mmap = pe_mmap,
    .unlocked_ioctl = pe_ioctl,
    .compat_ioctl = pe_ioctl,
};

static struct lgemu_cdev pe_cdev;

void lx_pe_remove(void)
{
    lgemu_cdev_del(&pe_cdev);
    lgemu_store_free(&pe_store);
}

int lx_pe_add(void)
{
    int err;

    if (pe_hdr & ~PAGE_MASK)
        return -EINVAL;
    if (pe_hdr) {
        u32 *header = lgemu_store_ptr(&pe_store, pe_hdr);

        if (!header)
            return -ENOMEM;
        pe_hdr_layout();
        header[0] = PE_HDR_VERSION; /* PE_HDR_HW_M17_InitHdrInfo */
    }
    err = lgemu_cdev_add(&pe_cdev, PE_MAJOR, 0, 1, "pe", &pe_fops);
    if (!err)
        pr_info("pe: pe0, settings accepted, levels 0, HDR map %#x\n", pe_hdr);
    return err;
}

/* ---- the gfx driver: /dev/lg/gfx0 ---- */

#define GFX_MAJOR 151

static struct lgemu_cdev gfx_cdev;

void lx_gfx_remove(void)
{
    lgemu_cdev_del(&gfx_cdev);
}

int lx_gfx_add(void)
{
    return lgemu_cdev_add(&gfx_cdev, GFX_MAJOR, 0, 1, "gfx", &lgemu_gfx_fops);
}
