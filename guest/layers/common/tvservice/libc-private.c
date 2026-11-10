// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* libtvservice-private.so: tvservice's private view of /dev/mem.
 *
 * It has no HAL boundary: tvservice opens /dev/mem itself. (Its eMMC partitions, /proc/partinfo and
 * the NVM's /dev/mmcblk0pN, are the VM's own now: lgemu.ko and guest/initramfs/rootfs/init) This
 * library is LD_PRELOADed into the two processes that link libhal_lg115x: tvservice
 * (etc/init/tvservice.override, after dbgfrwk's liblibm) and tvwin (etc/init/tvwin.override for its
 * factory job, and its on-demand LS2 service file under usr/share/dbus-1). Its definitions of
 * fopen/fopen64, open/open64/__open_2/
 * __open64_2, mmap/mmap64 and raise so come before libpthread's and libc's in their global scope
 * and are the ones every library in those processes calls; no other library in tvservice's
 * scope defines any of them. The constructor takes the library out of LD_PRELOAD again, so what
 * these processes start sees the environment of the stock jobs. Nothing else in the guest sees
 * any of this.
 *
 *   /dev/mem        opened by tvservice's capture code, libhal_lg115x, libkadaptor, liblxv and
 *                   libhalgal to map LG's register windows and frame buffers: the emulator's
 *                   physical memory instead (guest/layers/common/hal-common/physmem.c), where the
 * DILE_VT shim's frame buffers are, and whose graphics range is /dev/gfx (mmap64 below). A stream
 * (fopen) of /dev/mem still reads /dev/zero.
 *
 * open is a plain system call in glibc; these call libc's __open/__open64 aliases for any other
 * path, and fopen/fopen64 are looked up once with dlsym(RTLD_NEXT).
 *
 * Also raise, for one division by zero in tvservice's tv.capture (at the end of this file).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../hal-common/hal-common.h"
#include "../../../../include/webos-gfx.h"

int __open(const char *path, int flags, ...);
int __open64(const char *path, int flags, ...);

#define SELF "/usr/lib/libtvservice-private.so"

/* Out of LD_PRELOAD, in place (no allocation this early): the stock value stays for what the
 * process starts (tvservice's "/usr/lib/liblibm.so.1"), or none at all. */
__attribute__((constructor)) static void leave_preload(void)
{
    char *v = getenv("LD_PRELOAD"), *p;
    size_t n = strlen(SELF);
    if (!v)
        return;
    for (p = v; (p = strstr(p, SELF)); p++) {
        if ((p == v || p[-1] == ' ' || p[-1] == ':') && (!p[n] || p[n] == ' ' || p[n] == ':')) {
            char *from = p + n;
            if (p > v && !*from)
                p--; /* the last entry: drop the separator before it */
            else if (*from)
                from++; /* else the one after it */
            memmove(p, from, strlen(from) + 1);
            break;
        }
    }
    if (!v[strspn(v, " :")])
        unsetenv("LD_PRELOAD");
}

/* The system call itself: libc's mmap64 through dlsym would recurse, as dbgfrwk's calloc
 * (liblibm, LD_PRELOADed into tvservice) maps memory with mmap and dlsym allocates. */
static void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
                      unsigned long long offset)
{
    if (offset & 4095) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    return (void *)syscall(SYS_mmap2, addr, length, prot, flags, fd, (unsigned long)(offset >> 12));
}

void *mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset)
{
    int gfx = physmem_gfx_fd(fd, (unsigned long long)offset, length);
    if (gfx >= 0)
        return sys_mmap(addr, length, prot, flags, gfx,
                        (unsigned long long)offset - WEBOS_GFX_PHYS_BASE);
    return sys_mmap(addr, length, prot, flags, fd, (unsigned long long)offset);
}

/* 32-bit off_t: glibc passes it to mmap2 as unsigned, so addresses above 2 GiB work */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    return mmap64(addr, length, prot, flags, fd, (off64_t)(uint32_t)offset);
}

static int is_devmem(const char *path)
{
    static int logged;
    if (!path || strcmp(path, "/dev/mem"))
        return 0;
    if (!logged++)
        hal_log("/dev/mem opened: the emulator's physical memory instead (no LG register windows "
                "here)");
    return 1;
}

static int mode_arg(int flags, va_list ap)
{
    return flags & O_CREAT ? va_arg(ap, int) : 0;
}

int open(const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    va_start(ap, flags);
    mode = mode_arg(flags, ap);
    va_end(ap);
    if (is_devmem(path))
        return physmem_open(flags);
    return __open(path, flags, mode);
}

int open64(const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    va_start(ap, flags);
    mode = mode_arg(flags, ap);
    va_end(ap);
    if (is_devmem(path))
        return physmem_open(flags);
    return __open64(path, flags, mode);
}

int __open_2(const char *path, int flags)
{
    if (is_devmem(path))
        return physmem_open(flags);
    return __open(path, flags, 0);
}

int __open64_2(const char *path, int flags)
{
    if (is_devmem(path))
        return physmem_open(flags);
    return __open64(path, flags, 0);
}

typedef FILE *fopen_fn(const char *, const char *);

FILE *fopen64(const char *path, const char *mode)
{
    static fopen_fn *real;
    if (!real && !(real = (fopen_fn *)dlsym(RTLD_NEXT, "fopen64"))) {
        errno = ENOSYS;
        return NULL;
    }
    return real(is_devmem(path) ? "/dev/zero" : path, mode);
}

FILE *fopen(const char *path, const char *mode)
{
    static fopen_fn *real;
    if (!real && !(real = (fopen_fn *)dlsym(RTLD_NEXT, "fopen"))) {
        errno = ENOSYS;
        return NULL;
    }
    return real(is_devmem(path) ? "/dev/zero" : path, mode);
}

/* ---- raise: the division by zero in tv.capture's VIDEO method ----
 *
 * CAPTURE_VT_CaptureVideo scales the requested size to the main window's configured output
 * (API_VIDEO_GetOutputRegion(0): tvservice's own state, set only when a video window mode is
 * applied, i.e. when an input has a timing or a media window is placed) with
 * CAPTURE_Util_CalcSize, before its own no-signal check (CAPTURE_Pipeline_IsVideoOnScreen in
 * _CAPTURE_VT_Capture: CAPTURE_ERROR_09). On the analog tuner with no signal that output is
 * still 0x0, and the first request size, min(1280, 0) = 0, makes CalcSize divide by the window's
 * width: libgcc's __udivsi3 (the set's Cortex-A9 has no divide instruction) branches to
 * __aeabi_idiv0, which is raise(SIGFPE), and dbgfrwk's handler has the micom restart the set.
 * This is LG's code with the answers LG's own HAL gives there
 * (guest/layers/soc/src/libhal_lg115x/hal-avd.c), so a WT1H would do the same.
 *
 * The emulator does not restart for it: for that raise alone, from tvservice's __aeabi_idiv0
 * with CalcSize as the caller of the division, the quotient is 0 (the answer ARM's UDIV
 * instruction gives). CalcSize then leaves the size 0x0, CaptureVideo carries on to the no-signal
 * check, and tv.capture answers tvservice's own CAPTURE_ERROR_09, as lk5900 does with no video.
 * Every other raise, and every other division by zero, is LG's. Nothing is taken from one
 * firmware build: CAPTURE_Util_CalcSize and its size are tvservice's dynamic symbol (dlsym,
 * dladdr1), and __aeabi_idiv0 (a local symbol) is recognised by its instructions around raise's
 * return address in the main program, ARM (WT1H to W18R) or Thumb-2 (W19P, the same four
 * instructions). A tvservice without the symbol, or whose __aeabi_idiv0 is different code, keeps
 * LG's SIGFPE. W19P's CaptureVideo divides the same way with no video (the output region 0x0).
 * raise is also in libpthread and libc; this library comes before both (see the top of this file).
 */
#include <link.h>
#include <signal.h>

/* __aeabi_idiv0 as libgcc builds it for ARM: push {r1, lr}; mov r0, #8 (SIGFPE); bl raise;
 * pop {r1, pc}. raise returns to the pop; the bl's offset is whatever the link gave. */
#define IDIV0_PUSH  0xe92d4002u
#define IDIV0_MOV   0xe3a00008u
#define IDIV0_POP   0xe8bd8002u
#define ARM_BL_MASK 0xff000000u
#define ARM_BL      0xeb000000u
/* The same in Thumb-2: push {r1, lr}; mov.w r0, #8; bl or blx raise; pop {r1, pc}. raise returns
 * to the pop with the Thumb bit set. */
#define T_PUSH     0xb502u
#define T_MOVW0    0xf04fu
#define T_MOVW1    0x0008u
#define T_BL0_MASK 0xf800u
#define T_BL0      0xf000u
#define T_BL1_MASK 0xc000u
#define T_BL1      0xc000u
#define T_POP      0xbd02u

__attribute__((visibility("hidden"))) int tvservice_raise(int sig, const uint32_t *caller_sp,
                                                          uint32_t ret);

/* raise(sig) with the caller's sp and return address: __aeabi_idiv0 pushed {r1, lr}, so
 * caller_sp[1] is where the division returns to. */
__asm__("	.text\n"
        "	.arm\n"
        "	.align 2\n"
        "	.global raise\n"
        "	.type raise, %function\n"
        "raise:\n"
        "	mov r1, sp\n"
        "	mov r2, lr\n"
        "	b tvservice_raise\n"
        "	.size raise, . - raise\n");

/* The main program's executable segments: the first object dl_iterate_phdr reports. */
static int exe_text(struct dl_phdr_info *info, size_t size, void *data)
{
    uint32_t *range = data;
    int i;
    (void)size;
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X) &&
            range[2] >= info->dlpi_addr + ph->p_vaddr &&
            range[2] < info->dlpi_addr + ph->p_vaddr + ph->p_memsz) {
            range[0] = info->dlpi_addr + ph->p_vaddr;
            range[1] = range[0] + ph->p_memsz;
        }
    }
    return 1;
}

/* ret: raise's return address, called from the main program's __aeabi_idiv0, ARM or Thumb-2 */
static int from_idiv0(uint32_t ret)
{
    uint32_t range[3] = {0, 0, ret & ~1u};
    dl_iterate_phdr(exe_text, range);
    if ((ret & ~1u) < range[0] + 12 || (ret & ~1u) + 4 > range[1])
        return 0;
    if (ret & 1) { /* Thumb */
        const uint16_t *h = (const uint16_t *)(ret & ~1u);
        return h[-5] == T_PUSH && h[-4] == T_MOVW0 && h[-3] == T_MOVW1 &&
               (h[-2] & T_BL0_MASK) == T_BL0 && (h[-1] & T_BL1_MASK) == T_BL1 && h[0] == T_POP;
    }
    if (ret & 3)
        return 0;
    const uint32_t *w = (const uint32_t *)ret;
    return w[-3] == IDIV0_PUSH && w[-2] == IDIV0_MOV && (w[-1] & ARM_BL_MASK) == ARM_BL &&
           w[0] == IDIV0_POP;
}

int tvservice_raise(int sig, const uint32_t *caller_sp, uint32_t ret)
{
    static int (*real)(int);
    static int looked_up;
    static uint32_t calcsize, calcsize_end;
    if (sig == SIGFPE) {
        if (!looked_up) {
            void *f = dlsym(RTLD_DEFAULT, "CAPTURE_Util_CalcSize");
            const ElfW(Sym) *sym = NULL;
            Dl_info info;
            looked_up = 1;
            if (f && dladdr1(f, &info, (void **)&sym, RTLD_DL_SYMENT) && sym &&
                info.dli_saddr == f && sym->st_size) {
                calcsize = (uint32_t)f;
                calcsize_end = calcsize + sym->st_size;
            }
        }
        if (calcsize && caller_sp[1] > calcsize && caller_sp[1] < calcsize_end && from_idiv0(ret)) {
            hal_log("capture: CAPTURE_Util_CalcSize divided by 0 at 0x%08x (the main window's "
                    "output is 0x0): quotient 0 instead of SIGFPE",
                    caller_sp[1] - 4);
            return 0;
        }
    }
    if (!real && !(real = (int (*)(int))dlsym(RTLD_NEXT, "raise")))
        return (int)syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), sig);
    return real(sig);
}
