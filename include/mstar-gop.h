// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MSTAR_GOP_H
#define MSTAR_GOP_H
#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef u32 mstar_gop_u32;
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint32_t mstar_gop_u32;
#endif
/* The GOP frame buffer of mstar-utopia.ko (its parameter gop_fb, the mmapger record of
 * GOP_FRAMEBUFFER): MStar's graphics output memory and its planes, for LG SimpleSmart
 * (docs/dev/architecture/simplesmart.md, "Display"). This is the emulator's shared ioctl contract,
 * distinct from the stock vendor ioctl ABI. Three users, one ABI:
 *   - our libhalgal (guest/layers/simplesmart/src/libhalgal), in procmgr and RELEASE: the HAL_GAL_*
 *     functions draw in software into the pool and say which surface each GOP controller shows;
 *   - LG's DirectFB (devmem system), which maps the same pool from /dev/fb0 at its PA;
 *   - the initramfs's presenter (guest/initramfs/src/gop-present.c), which copies the shown planes
 * to virtio-gpu. The node is /dev/mstar-gop-fb on devtmpfs; init binds it over the firmware's
 * /dev/fb0.
 *
 * mmap: the pool from its start, as SS1A's stock users map /dev/fb0 (/dev/gfx) and add a
 * surface's offset: at file offset 0 (LG's GX client, libgmopenapi) or at the pool's PA, the
 * capability's physicalAddrOfBase (procmgr). A file offset below the pool's size is an offset in
 * the pool, one from its PA a PA in it. Shared mappings only. The pool is the same pages
 * as /dev/malloc's view of that record. Every field is little-endian, 32 bits, the same layout for
 * the 32-bit firmware and the arm64 presenter. */

/* The GOP controllers, by MStar's GOP number, which is what the firmware passes as HAL_GAL's FB HW
 * controller index (SS1A's KADP sets up GOP 0, the OSD, and 2, the cursor; M2 has four) */
#define MSTAR_GOP_PLANES 4

/* HAL_GAL_PIXEL_FORMAT_T values (hal_gal.h; SimpleSmart's libhalgal: bpp 32, 8, 16, 16, 16, 8) */
enum mstar_gop_format {
    MSTAR_GOP_ARGB8888 = 0,
    MSTAR_GOP_LUT8 = 1,
    MSTAR_GOP_ARGB1555 = 2,
    MSTAR_GOP_RGB565 = 3,
    MSTAR_GOP_ARGB4444 = 4,
    MSTAR_GOP_A8 = 5,
};

struct mstar_gop_pool { /* GET_POOL: the pool */
    mstar_gop_u32 pa, size;
};

struct mstar_gop_alloc { /* ALLOC: in size (bytes) and align (a power of 2, 0 = page);
                          * out pa. -ENOMEM when no free range fits */
    mstar_gop_u32 size, align, pa;
};

/* A GOP controller's plane: what HAL_GAL_SetFrameBuffer*, SetFBHWControllerVisibility and
 * SetFrameBufferProperty set. view: the part of the surface shown (x, y, w, h; w = 0: all of it);
 * disp: where on the output (w = 0: the whole output). gen: the device's generation when it was
 * last set (out only). */
struct mstar_gop_plane {
    mstar_gop_u32 index;   /* 0 .. MSTAR_GOP_PLANES - 1 */
    mstar_gop_u32 enabled; /* a surface is set (SetFrameBuffer; ResetFrameBuffer clears) */
    mstar_gop_u32 visible;
    mstar_gop_u32 pa, width, height, pitch, format;
    mstar_gop_u32 view[4], disp[4];
    mstar_gop_u32 gen;
};

struct mstar_gop_wait { /* WAIT: until the generation is not gen, or timeout_ms pass
                         * (at most 1000); out gen, the current one */
    mstar_gop_u32 gen, timeout_ms;
};

struct mstar_gop_mem { /* MEM_INFO: the allocator's bytes */
    mstar_gop_u32 total, allocated, free, largest_free;
};

#define MSTAR_GOP_IOC_GET_POOL  _IOR('G', 0x40, struct mstar_gop_pool)
#define MSTAR_GOP_IOC_ALLOC     _IOWR('G', 0x41, struct mstar_gop_alloc)
#define MSTAR_GOP_IOC_FREE      _IOW('G', 0x42, mstar_gop_u32) /* an ALLOC's pa */
#define MSTAR_GOP_IOC_SET_PLANE _IOW('G', 0x43, struct mstar_gop_plane)
#define MSTAR_GOP_IOC_GET_PLANE _IOWR('G', 0x44, struct mstar_gop_plane) /* in index */
#define MSTAR_GOP_IOC_DAMAGE    _IO('G', 0x45) /* a shown plane's pixels changed: a new generation */
#define MSTAR_GOP_IOC_WAIT      _IOWR('G', 0x46, struct mstar_gop_wait)
#define MSTAR_GOP_IOC_MEM_INFO  _IOR('G', 0x47, struct mstar_gop_mem)
#endif
