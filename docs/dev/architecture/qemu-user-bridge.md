# The 32-bit userland under qemu-user (the compat bridge)

Every firmware's userland is 32-bit ARM, and the arm64 guest kernel normally runs it in AArch32
EL0. A host whose CPU has no AArch32 at EL0 (Apple silicon) cannot, so a VM there would be fully
emulated. With `userland = "qemu-user"` the arm64 guest kernel still runs under the hardware
hypervisor, and each 32-bit program runs under `qemu-arm` inside the guest (`"auto"`, the default,
does that by itself on such a host). qemu-arm translates
the instructions, but it does not convert the system calls: it hands them to the guest kernel's
own AArch32 compat syscall table. The study behind it, with measurements and how translation was
verified, is [docs/research/qemu-user-bridge.md](../../research/qemu-user-bridge.md).

## Why a bridge

Stock qemu-user converts every system call itself. It returns ENOTTY for any ioctl missing from
its table, which has almost no DRM and no evdev, uinput, V4L2, ALSA or LG driver entries, so
webOS's compositor crash-loops. A passthrough would not be enough either: evdev returns 24-byte
records to a 64-bit reader and 16-byte ones to a 32-bit one, and our modules
(`guest/kernel/modules/`) read 32-bit layouts through `compat_ptr()` and `in_compat_syscall()`.
The arm64 kernel already has the whole 32-bit ABI as plain C (the compat table and every
`compat_ioctl` converter), and none of it needs a CPU with AArch32. The bridge lets a 64-bit
process enter it.

## The pieces

- **Guest kernel, system call `0x7ff0`** (`guest/kernel/patches/0023-arm64-compat-bridge-syscall.patch`):
  it takes an AArch32 call number and r0 to r6, packed two per register, and runs
  `compat_sys_call_table[nr]` with TIF_32BIT set for that call only. The number lies above the
  native table, so only a 64-bit task reaches it (through `do_ni_syscall()`), and a task that never
  calls it sees no change. It refuses, with ENOSYS, the calls whose effect would outlive the flag
  or depend on the caller's registers: restart_syscall, exit, exit_group, fork, vfork, clone,
  clone3, execve, execveat, ptrace, sigreturn, rt_sigreturn, personality and sigaltstack. Seccomp
  and audit see the outer call.
- **Guest kernel, binfmt_misc flag `E`** (`0024-binfmt-misc-exe-flag.patch`; it implies `O`): the
  process's executable (`mm->exe_file`, `/proc/<pid>/exe`) is the binary rather than its
  interpreter. ls-hubd identifies a client by its executable and finds its role files by that
  path, so without `E` it would refuse every webOS service. An entry registered without `E`
  behaves as before. `CONFIG_BINFMT_MISC=y`.
- **qemu-arm** (beanjar-vendor's `qemu-user/`, QEMU 10.0.2 linux-user, one static aarch64 program):
  when guest addresses equal host addresses (`guest_base` 0) and the kernel answers the call, it
  forwards every system call through `0x7ff0` except those it must own. `bridge_keep()` in its
  `linux-user/compat-bridge.c.inc` is the only list of those calls:
  - tasks and programs, which are qemu's CPU state, threads and its own exec: exit, exit_group,
    fork, vfork, clone, clone3, execve, execveat, set_tid_address, restart_syscall, ptrace,
    personality;
  - signals, because qemu delivers them and its guest-to-host signal numbers are not the identity
    (real-time signals are shifted and SIGABRT is remapped): the sigaction, sigprocmask,
    sigsuspend, sigpending, sigtimedwait, sigqueueinfo, sigaltstack and sigreturn families,
    signalfd, kill, tkill, tgkill, pidfd_send_signal, pause, wait4, waitid, ppoll, pselect6,
    epoll_pwait, timer_create, mq_notify, prctl, and `fcntl` F_SETSIG and F_GETSIG;
  - the address space, which qemu's page flags and translated code follow: mmap2, munmap,
    mprotect, mremap, brk, shmat, shmdt, madvise, remap_file_pages;
  - its file-descriptor translators and faked `/proc` files: close, close_range, dup, dup2, dup3,
    open, openat, openat2, readlink, readlinkat, and read, write and the socket calls on a
    descriptor that has a translator;
  - what it fakes for the emulated CPU: uname (armv7l), ugetrlimit, setrlimit, prlimit64.

  `QEMU_BRIDGE=0` turns the bridge off, and `QEMU_BRIDGE=1` makes qemu say why it could not turn
  it on. qemu also copies what it logs as unimplemented to `/dev/kmsg`, once per message and
  process, so a system's gaps show in `dmesg` as `qemu-arm[<pid> <program>]: ...`
  (`QEMU_UNIMP_KMSG=0` turns that off).

## The setting

```toml
[hardware]
userland = "qemu-user"   # auto (default, not written), native or qemu-user
```

`beanjar settings NAME set userland=qemu-user`; the launcher's variable is `USERLAND`. `auto` is
resolved at each start by `vmsettings.resolve_userland`, and the launcher logs the result and the
reason in one line (`userland: qemu-user (KVM on a CPU without AArch32 at EL0)`):

| Guest kernel and accelerator | userland |
|---|---|
| arm64 under TCG | native: TCG emulates AArch32 itself |
| arm64 under KVM, the CPU has AArch32 at EL0 (Raspberry Pi 5, RK3399) | native |
| arm64 under KVM, the CPU has no AArch32 at EL0 | qemu-user |
| arm64 under HVF (Apple silicon; not in the launcher yet) | qemu-user |
| x86-64 (`guest_kernel`, below) | qemu-user |

Whether the CPU has AArch32 at EL0 is the host kernel's own answer (`qemu.host_aarch32_el0`): on
arm64, `personality(PER_LINUX32)` fails with EINVAL unless `system_supports_32bit_el0()`, the
sanitised `ID_AA64PFR0_EL1.EL0` of all CPUs (`arch/arm64/kernel/sys.c`), which is also what KVM
offers its guests. It is asked in a child process. Then the firmware's own constraint applies
(below): NetCast and SimpleSmart need native, and where the host rule gives qemu-user they do not
start, with an error saying the firmware needs native 32-bit execution, which this host cannot
provide. An explicit `native` or `qemu-user` is taken as it is.

`qemu.plan` finds the interpreter with `paths.Tree.qemu_arm`: `QEMU_ARM=PATH` (a local build, for
development until beanjar-vendor publishes a release), else the vendored one, which the host build
publishes once `packaging/vendor.toml` pins `qemu-user` (`meson/vendor.py`): build/qemu-user, its
aarch64 program, for the arm64 guest kernel on every host, and on amd64 hosts also
build/qemu-user-amd64, its x86-64 one, for the x86-64 guest kernel (`guest = ["arm64", "amd64"]`;
`BEANJAR_VENDOR_QEMU_USER` and `BEANJAR_VENDOR_QEMU_USER_AMD64` take a CI artifact instead). The
Debian packages do not ship it; an installed launcher looks for
`/usr/share/beanjar/guest/bin/qemu-arm`. A launch without an interpreter fails. `share.assemble`
copies the interpreter to `/host/qemu-arm`, and boot.conf gets `USERLAND="qemu-user"`. The QEMU
command line does not change.

`guest/initramfs/rootfs/init` reads boot.conf, and before it runs any firmware program it copies
the interpreter into the initramfs and registers it for 32-bit little-endian ARM ELF executables
with the flags `POFE`. `P` keeps argv[0], `O` opens the binary, `F` opens the interpreter at
registration so it serves the firmware's namespace and root, and `E` is described above. Every
32-bit program of the firmware then runs under qemu-arm, init included. Its `binfmt` function
registers any interpreter the launcher puts into `/host`; the AArch64 programs of newer firmware
(`/usr/lib64` on queue and spacebuns) would take a second entry there, `qemu-aarch64` on the x86-64
kernel with flag `E` and their ioctls passed through unchanged.

To check that nothing runs natively, add `arm64.no32bit_el0` to the kernel command line
(`KERNEL_ARGS`). The kernel then reports no 32-bit EL0, so a native 32-bit exec fails with
ENOEXEC, and any program that escaped binfmt_misc would fail to start.

## Where it is refused

`vmsettings.NATIVE_ONLY`, firmware lines that need native 32-bit execution whatever the host can
do: an explicit `qemu-user` and `guest_kernel = "x86-64"` are refused by `beanjar settings` and at
launch, and `auto` fails the start where the host cannot run them natively:

- **NetCast** (LM14, NetCast 4.5): the stock application writes to MMIO windows
  (`/dev/miomap`: HWI2C, OTP, PQ), whose pages mstar-utopia keeps unmapped so that every access
  faults into lgemu's `store_mmio_fault`. That handler decodes the faulting AArch32 instruction,
  which a translated program does not have: the faulting task is qemu-arm (64-bit) and the
  instruction is qemu's AArch64 code, so the handler logs `lgemu: unsupported MMIO` and sends
  SIGBUS. qemu emits plain loads and stores for guest memory accesses, so supporting it would take
  an A64 LDR/STR decoder for 64-bit tasks in the same handler. No other module decodes user
  instructions, and webOS targets do not map these windows.
- **SimpleSmart**: not tried.

webOS and the test guest are allowed.

## Tests

`tests/integration/bridge` boots `build/Image` in qemu-system-aarch64 under full TCG, with a small
initramfs holding busybox, qemu-arm and `bridgetest.c` built static for 32-bit ARM. It runs
bridgetest natively, under qemu-arm with `QEMU_BRIDGE=0`, and through binfmt_misc `POFE`.
bridgetest checks uinput and evdev with 16-byte events, DRM and virtio-gpu ioctls, a robust mutex
whose owner died, signal restart and EINTR, a real-time signal, fork, exec and wait, and
`/proc/<pid>/exe`. It skips without qemu-arm (`QEMU_ARM`) or a static 32-bit ARM compiler
(`BRIDGE_CC`, the webOS SDK's or Debian's).

## The x86-64 guest kernel (experimental)

`guest_kernel = "x86-64"` (`GUEST_KERNEL`) runs the same idea on an x86-64 host: an x86-64 guest
kernel on QEMU's q35 machine, under KVM where `/dev/kvm` opens and TCG otherwise, with the whole
ARM userland under an x86-64 qemu-arm. The study is
[An x86-64 guest kernel](../../research/qemu-user-bridge.md#an-x86-64-guest-kernel-2026-11-15).
It is refused on another host, with `userland = "native"` and on NetCast and SimpleSmart, and a
start says it is experimental.

- **Kernel**: the same tree, patches and `webos_virt_defconfig` with
  `guest/kernel/configs/x86_64.config` over it, built with `ARCH=x86_64` by the build machine's
  compiler (`meson/kbuild.sh` with `GUEST_ARCH=x86_64`) on x86-64 build hosts (meson option
  `x86_64_kernel`, auto), with the out-of-tree modules, busybox and the initramfs programs:
  build/x86_64/bzImage and build/x86_64/initramfs.cpio.gz, from the same `initramfs.list` and
  `/init`. Patch 0025 adds system call `0x7ff0` to `do_syscall_64`: an i386 call number and five
  arguments, run by `ia32_sys_call()` with `TS_COMPAT` set for that call only, for ioctl, read,
  write, readv, writev, set_robust_list and get_robust_list; anything else is ENOSYS. Patch 0026
  registers the media pool's ivshmem driver at `device_initcall` on x86, where ACPI claims PCI
  resources later. lgemu's AArch32 MMIO decoder is arm64-only (hence NetCast's refusal).
- **qemu-arm** (beanjar-vendor `qemu-user/`, patch 0002, the amd64 tarball): the kernel's compat
  layer here is i386's, so qemu converts ordinary calls itself and hands the kernel only those
  whose meaning depends on a 32-bit caller. ioctls whose ARM layout equals x86-64's run natively;
  SNDRV_CTL ELEM_READ/WRITE and VIDIOC_DQEVENT are repacked to i386's layout.
- **Machine** (`qemu.plan`): `-M q35`, `-accel kvm -cpu host` (or `-cpu max -accel tcg`),
  `-vga none`, `console=ttyS0`, and the virt machine's devices, which are all PCI; q35's root bus
  has 30 free slots. Its QEMU is the vendored qemu-system-x86_64 (beanjar-vendor's amd64 QEMU
  tarball) or `QEMU_X86_64`, never one from PATH, which would lack the display patches.
