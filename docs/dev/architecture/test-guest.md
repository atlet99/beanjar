# The open test guest

Public CI may not use LG firmware, so without something else it cannot boot a VM. The test guest
is that something else: a small, freely licensed 32-bit ARM userland, booted by the guest kernel
through the normal launcher as a target of its own, which runs a few checks and reports them as
pass or fail with numbers. It is not meant to look like webOS. It covers the launcher, QEMU's
devices and the host side: virtio-gpu with virgl, the virtio-serial ports, the 9p shares, input,
the 32-bit ABI through the kernel's compat table (natively or under qemu-user) and the guest's own
power-off.

## Two parts: the userland and the checks

The test guest is split by how often each part changes.

- **The userland** is beanjar-vendor's program `testguest` (its `testguest/README.md`): a Buildroot
  root filesystem, the sysroot to compile against it, the pin of the toolchain that does, and
  Buildroot's legal-info, released like the other vendored programs. It changes when Buildroot or
  Mesa does, rarely, and a cold Buildroot build is most of half an hour on a CI runner.
- **The checks** (`guest/testguest`, GPL-2.0-or-later) stay here: they speak `include/webos-media.h`,
  `include/webos-video.h` and the port protocol, so they change with the host. They are compiled
  against the vendored sysroot and delivered as the target's one always-used layer, `testguest`,
  the way every target gets its layers.

Decided for the userland (beanjar-vendor's `testguest/defconfig`, Buildroot 2026.02.3, the LTS line):

- **armv7 hard-float, glibc.** The firmware's userland is 32-bit ARM with glibc, so a check of the
  32-bit ABI on the arm64 kernel (`CONFIG_COMPAT`) means the same thing here as for webOS. The
  toolchain is Buildroot's Bootlin external one (armv7-eabihf glibc stable 2025.08-1, GCC 14,
  glibc 2.41), which saves building GCC; the CPU is Cortex-A7 with NEON-VFPv4, which every host CPU
  here runs in AArch32 EL0 and TCG emulates. Bootlin's toolchains run on x86_64 only, so the test
  guest is built on x86_64 hosts.
- **BusyBox init**, as NetCast 4.5's: `INIT_SYSTEM=sysvinit`, so the initramfs's existing sysvinit
  path starts it in the firmware's namespace like any other target; Buildroot's `rcS` runs the
  layer's `/etc/init.d/S99testguest`.
- **Mesa 26.0 with the virgl gallium driver only**, EGL, GLES and GBM; no LLVM, Vulkan, X11 or
  Wayland. A check draws offscreen on the render node; nothing is scanned out.

### Where it comes from

`packaging/vendor.toml` pins it like QEMU: a table `[testguest]` with the release tag
(`testguest-<version>`) and `guest = "armhf"`, the guest's architecture, with the SHA-256 of its one
tarball, which every host takes (`meson/vendor.py`). Before a release, or to try another build,
`BEANJAR_VENDOR_TESTGUEST=<path to testguest-<version>-armhf.tar.xz>` takes that tarball instead,
pinned or not: for example the artifact of a beanjar-vendor CI run,
`gh run download <run> -R mariotaku/beanjar-vendor -n beanjar-testguest-armhf`. A new release is a pin
change only.

`scripts/testguest-setup` takes the userland into `build/vendor`, and the toolchain its `BUILDINFO`
pins (Bootlin's tarball, by SHA-256) beside it, and writes:

| Path | What |
|---|---|
| `build/testguest/image` | `rootfs.pak` (the userland's root filesystem) and `manifest.toml` naming the target: a firmware directory for `beanjar create --firmware` |
| `build/testguest/cross.ini` | the cross file of the guest build of this target: the toolchain, `--sysroot` and the CPU options, and pkg-config into the sysroot |

The guest build of the target (`-Dsoc=virt -Dwebos=testguest`, `meson.build`'s `testguest`) enters
`guest/testguest`, `guest/kernel` and `guest/initramfs`: the `testguest` layer and the arm64 pieces
every target shares (Debian's aarch64 GCC). The Image and initramfs it publishes are those of any
other target's build. `scripts/build-packages.sh guest` skips the target: nothing of it is packaged.

The `testguest` layer (`build/components/release/testguest`) holds the checks, `tg-gpu`, `tg-ports`,
`tg-input` and `tg-bridge` in `/usr/bin` (stripped, held to glibc 2.41 like every layer), and
`guest/testguest/rootfs`: the runner `tg-run`, its init script and the board file.

## The target `virt-testguest`

`guest/targets/virt-testguest.toml` and `guest/targets/release/testguest.toml`: chip `virt`, release
`testguest`, platform `Test`, model `TESTGUEST` (a name, not LG's; in `bring_up`, so `scripts/regress`
leaves it out of its default set: its runner is `scripts/testguest-check`). The release has one pak,
`rootfs` at `/`, one always-used layer, `testguest`, no state disks, 512 MiB (`dram`), a 1280x720 display (`screen`), and
neither `partmap` nor `nvm`: it has no LG board data. Its board file is
`guest/testguest/rootfs/etc/board.cmdline`.

The special cases are these, each keyed on the release having no board data
(`targets.board_data`) or on the platform `Test`:

- `prepare.py` makes no partition map and no EEPROM (no firmware code runs at create), and its
  record says `board_data = false`; `qemu.plan` attaches no partinfo disk and `prepare_nvm` does
  nothing; `boot.conf` has empty `NVM_EEPROM` and `PARTINFO_DISK`, which the initramfs already skips.
- `newvm` gives a VM of the platform no optional layers unless `--layers` says otherwise (the
  default `normal,gl` are webOS's).
- Power-off (`targets.takes_power_off`, `poweroff.power_off_call`): `beanjar stop` types
  `timeout 30 webos-enter poweroff` on the initramfs console instead of tvpowerd's call, and does
  not wait for the micom's `ready=1`, which only webOS's power stack sets. BusyBox's init then runs
  its shutdown and calls `reboot(2)` inside the PID namespace; the kernel ends the namespace's init
  with SIGINT and `webos-start` powers the machine off, so QEMU exits by itself: the same path a
  webOS shutdown takes. Chosen over QMP `system_powerdown` (the virt machine's power button is the
  micom's wake line here, and the guest has no ACPI) and over stopping QEMU, which would not test
  the guest's side. The launcher picks the call by the running launch's `vm-info.toml` platform.

## The checks

`/etc/init.d/S99testguest` starts `tg-run` once at the end of the boot. It runs each check, collects
their `key=value` lines and writes them to the out share as `testguest/results.txt` (written as
`results.tmp` and renamed, so the host never reads half a file) and to the console (`webos.log`).
Each check adds `<check>=pass` or `fail`; `result=pass` when all passed.

| Check | What it does | Numbers |
|---|---|---|
| `gpu` (`tg-gpu.c`) | GBM on `/dev/dri/renderD128`, an EGL GLES 2 context (surfaceless), a 256x256 offscreen framebuffer cleared and drawn 100 times, the centre pixel read back; passes when it is the colour drawn and the renderer is virgl | `gpu.renderer`, `gpu.frame_ms` (draw and `glFinish`) |
| `ports` (`tg-ports.c`) | every port of `qemu.py` by name (14); on `webos.media.0` a decode session of `include/webos-media.h` (OPEN, CAPS, DECODE, EOS) that the host's media helper answers with a PICTURE marked LAST; one SYNC frame of `include/webos-video.h` on `webos.video`, which is guest to host only | `ports.found`, `ports.media_rtt_ms` |
| `shares` (`tg-run`) | the read-only `host` share's `boot.conf` read and a write there refused; 32 MiB written to the `out` share, read back and compared | `shares.out_write_ms`, `shares.out_read_ms` |
| `input` (`tg-input.c`) | the evdev node of QEMU's virtio keyboard; once open, it creates `testguest/input-ready` and waits up to 60 s for a key | `input.key`, `input.wait_ms` |
| `bridge` (`tg-bridge.c`, the same program as `tests/integration/bridge/bridgetest.c`) | the 32-bit ABI through the kernel's compat table: uinput and evdev, DRM and virtio-gpu ioctls, a robust mutex, signals, fork, exec and wait, `/proc/<pid>/exe`; after `input`, as it creates an input device of its own | `bridge.userland` (`native`, or `qemu-user` under qemu-arm), `bridge.failed` |

`webos.ir` and `webos.memory` are held by the initramfs's pumps and `webos.standby` must not be
opened (holding it open is the guest's standby), so the check only finds them. Times in the guest
are its own clock, which the launcher scales under TCG (`webos.time_scale`).

## The test command

`scripts/testguest-check` is what CI runs, and works the same on a workstation: it creates a VM on
the image (`--display egl-headless --mode direct`; default name `testguest-check`, `--library` for
a library of its own), starts it, sends keys over QMP `input-send-event` (the keyboard the viewer
and VNC reach) once `input-ready` appears, waits for `results.txt`, then adds the host's own checks:

- `host.micom_port`: `run/micom-status` has the line the initramfs's IR pump writes on
  `webos.micom` at boot (the IR pump's other direction).
- `host.poweroff`: `beanjar stop --no-fallback` returns with "QEMU exited ... after the call", the
  guest's own power-off and nothing else; `host.poweroff_s` its time.

`--userland qemu-user` sets the VM's `hardware.userland`, so the guest's 32-bit programs run under
qemu-arm with the compat bridge ([qemu-user bridge](qemu-user-bridge.md); qemu-arm from `QEMU_ARM`),
which the `bridge` check reports as `bridge.userland=qemu-user`.

It prints every result, deletes the VM (unless `--keep`; `--logs DIR` keeps its logs and the guest's
output) and exits 0 when all passed, 1 when one failed, 2 when the run itself failed. One run on the
x86 PC (TCG, virgl on the d3d12 renderer, 2026-11-15):

```
boot.uptime_ms=2520
gpu.renderer=virgl (D3D12 (the PC's GPU))
gpu.gl_version=OpenGL ES 3.0 Mesa 26.0.1
gpu.pixel=3399ccff
gpu.frame_ms=4.686
ports.found=14
ports.media_rtt_ms=231.603
shares.out_write_ms=1400
shares.out_read_ms=4070
input.key=30
input.wait_ms=202.2
bridge.userland=native
bridge.failed=0
host.start_s=5.0
host.results_s=20.1
host.poweroff_s=6.5
result=pass
```

The whole command took 32 s.

## CI

`.github/workflows/testguest.yml` runs `scripts/testguest-ci.sh` on GitHub's `ubuntu-24.04` runner
(x86_64, TCG) in a `debian:trixie` container, as the other workflows do, on pushes to `main` and
pull requests that touch the guest, the launcher, the wire formats or the build, and by hand. The
script installs the `guest` and `host` build-dependency roles, runs `scripts/testguest-setup`, builds
the testguest layer, the kernel and initramfs, and the host build (the prebuilt QEMU, whose
surfaceless EGL display gives virgl Mesa's software rasteriser on a runner without a GPU, and the
media helper), then runs the check in a library under `build/testguest`. It builds no Buildroot:
that is beanjar-vendor's job. The workflow caches the vendored downloads (`build/vendor/download`:
the userland, its toolchain and QEMU, keyed by `packaging/vendor.toml`) and Meson's package cache,
and keeps the results and the VM's logs as the run's artifact.

The script was run in a fresh `debian:trixie` container on the PC (2026-11-15; no GPU passed in, as
on the runner; nothing cached, the userland a local beanjar-vendor build named by
`BEANJAR_VENDOR_TESTGUEST`): packages 96 s, the userland and Bootlin's toolchain 118 s (most of it
the toolchain's 83 MB download, which CI caches), the layer, kernel and initramfs 140 s, the host
build 7 s, the check 25 s: 386 s in all, where the Buildroot build alone had taken 13 min. Every
check passed there on virgl over llvmpipe (`gpu.renderer=virgl (LLVMPIPE (LLVM 19.1.7, 256 bits))`,
OpenGL ES 3.2, `gpu.frame_ms=11.050`, `ports.media_rtt_ms=3.028`, `bridge.failed=0`,
`host.poweroff_s=4.9`).

## Licensing

Everything in the root filesystem is built by Buildroot from upstream releases (BusyBox, glibc,
Mesa, libdrm, expat, zlib and the toolchain's runtime); the layer adds our checks. No LG file, font or
key. beanjar-vendor releases Buildroot's legal-info beside the userland
(`testguest-<version>-legal-info.tar`: each package's licence and source); a copy of the image
passed on should come with it.
