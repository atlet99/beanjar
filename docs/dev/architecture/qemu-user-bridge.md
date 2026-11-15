# The 32-bit userland under qemu-user (the compat bridge)

Every firmware's userland is 32-bit ARM, and the arm64 guest kernel normally runs it in AArch32
EL0. A host whose CPU has no AArch32 at EL0 (Apple silicon) cannot, so a VM there would be fully
emulated. With `userland = "qemu-user"` the arm64 guest kernel still runs under the hardware
hypervisor, and each 32-bit program runs under `qemu-arm` inside the guest. qemu-arm translates
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
userland = "qemu-user"   # native (default, not written) or qemu-user
```

`beanjar settings NAME set userland=qemu-user`; the launcher's variable is `USERLAND`. `qemu.plan`
finds the interpreter with `paths.Tree.qemu_arm`: `QEMU_ARM=PATH` (a local build, for development
until beanjar-vendor publishes a release), else the vendored `build/qemu-user/bin/qemu-arm`,
which the host build publishes once `packaging/vendor.toml` pins `qemu-user` with `guest = true`
(one arm64 tarball for every host, `meson/vendor.py`). The Debian packages do not ship it; an
installed launcher looks for `/usr/share/beanjar/guest/bin/qemu-arm`. A launch without an
interpreter fails. `share.assemble` copies the
interpreter to `/host/qemu-arm`, and boot.conf gets `USERLAND="qemu-user"`. The QEMU command line
does not change.

`guest/initramfs/rootfs/init` reads boot.conf, and before it runs any firmware program it copies
the interpreter into the initramfs and registers it for 32-bit little-endian ARM ELF executables
with the flags `POFE`. `P` keeps argv[0], `O` opens the binary, `F` opens the interpreter at
registration so it serves the firmware's namespace and root, and `E` is described above. Every
32-bit program of the firmware then runs under qemu-arm, init included.

To check that nothing runs natively, add `arm64.no32bit_el0` to the kernel command line
(`KERNEL_ARGS`). The kernel then reports no 32-bit EL0, so a native 32-bit exec fails with
ENOEXEC, and any program that escaped binfmt_misc would fail to start.

## Where it is refused

`vmsettings.USERLAND_REFUSED`, checked by `beanjar settings` and at launch:

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
