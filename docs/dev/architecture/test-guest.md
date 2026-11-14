# The open test guest

Public CI may not use LG firmware, so without something else it cannot boot a VM. The test guest
is that something else: a small, freely licensed 32-bit ARM userland built from source in this
repository, booted by the guest kernel through the normal launcher as a target of its own, which
runs a few checks and reports them as pass or fail with numbers. It is not meant to look like
webOS. It covers the launcher, QEMU's devices and the host side: virtio-gpu with virgl, the
virtio-serial ports, the 9p shares, input and the guest's own power-off.

## The userland: Buildroot

`guest/testguest` is a Buildroot `BR2_EXTERNAL` tree, built by `guest/testguest/build.sh` from the
Buildroot release pinned in that script (2026.02.3, the LTS line; tarball checked by SHA-256 and
downloaded once, not vendored). Everything goes under `build/testguest` (or the script's argument):
Buildroot's tree and output, `dl` and `ccache` (`BR2_DL_DIR` and `BR2_CCACHE_DIR` override them,
which CI keeps between runs), and what it publishes:

| Path | What |
|---|---|
| `build/testguest/image` | `rootfs.pak` (Buildroot's squashfs, LZO) and `manifest.toml` naming the target: a firmware directory for `beanjar create --firmware` |
| `build/testguest/legal-info` | Buildroot's `make legal-info`: the licence manifest, the licence texts and the sources of everything in the image |
| `build/testguest/cross.ini` | the cross file of this target's kernel and initramfs build (below) |

Decided:

- **armv7 hard-float, glibc.** The firmware's userland is 32-bit ARM with glibc, so a check of the
  32-bit ABI on the arm64 kernel (`CONFIG_COMPAT`) means the same thing here as for webOS. The
  toolchain is Buildroot's Bootlin external one (armv7-eabihf glibc stable, GCC 14, glibc 2.41),
  which saves building GCC; the CPU is Cortex-A7 with NEON-VFPv4, which every host CPU here runs
  in AArch32 EL0 and TCG emulates.
- **BusyBox init**, as NetCast 4.5's: `INIT_SYSTEM=sysvinit`, so the initramfs's existing sysvinit
  path starts it in the firmware's namespace like any other target.
- **Mesa 26.0 with the virgl gallium driver only**, EGL, GLES and GBM; no LLVM, Vulkan, X11 or
  Wayland. A check draws offscreen on the render node; nothing is scanned out.
- **The checks are ours** (`guest/testguest/src`, GPL-2.0-or-later), a Buildroot package
  (`package/beanjar-testguest`, a local source) built against the repository's `include/` wire
  formats. `overlay/` holds the runner and its init script.

Measured on the x86 PC (2026-11-15): a cold build (downloads cached, no ccache) takes 13 min 43 s
of wall time, 693 MB peak RSS; a rebuild of the checks and the image, 25 to 30 s. The image is
6.8 MB; Buildroot's output tree 2.4 GB, the legal-info 199 MB (hard links into `dl`).

## The target `virt-testguest`

`guest/targets/virt-testguest.toml` and `guest/targets/release/testguest.toml`: chip `virt`, release
`testguest`, platform `Test`, model `TESTGUEST` (a name, not LG's; in `bring_up`, so `scripts/regress`
leaves it out of its default set: its runner is `scripts/testguest-check`). The release has one pak,
`rootfs` at `/`, no layers, no state disks, 512 MiB (`dram`), a 1280x720 display (`screen`), and
neither `partmap` nor `nvm`: it has no LG board data. Its board file is
`guest/testguest/overlay/etc/board.cmdline`.

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

The kernel and initramfs are the ones every target uses. For CI without LG's SDK, the guest build
of this target (`-Dsoc=virt -Dwebos=testguest`, `meson.build`'s `testguest`) enters only
`guest/kernel` and `guest/initramfs`, and its cross file is `build/testguest/cross.ini`: Buildroot's
toolchain, which the build only has to find (the arm64 parts use Debian's aarch64 GCC). The Image
and initramfs it publishes are byte for byte those of any other target's build (checked on the PC).
`scripts/build-packages.sh guest` skips the target: nothing of it is packaged.

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

It prints every result, deletes the VM (unless `--keep`; `--logs DIR` keeps its logs and the guest's
output) and exits 0 when all passed, 1 when one failed, 2 when the run itself failed. One run on the
x86 PC (TCG, virgl on the d3d12 renderer, 2026-11-15):

```
boot.uptime_ms=2380
gpu.renderer=virgl (D3D12 (the PC's GPU))
gpu.gl_version=OpenGL ES 3.0 Mesa 26.0.1
gpu.pixel=3399ccff
gpu.frame_ms=10.221
ports.found=14
ports.media_rtt_ms=160.337
shares.out_write_ms=1560
shares.out_read_ms=3930
input.key=30
input.wait_ms=36.4
host.start_s=5.1
host.results_s=19.2
host.poweroff_s=6.6
result=pass
```

The whole command took 32 s.

## CI

`.github/workflows/testguest.yml` runs `scripts/testguest-ci.sh` on GitHub's `ubuntu-24.04` runner
(x86_64, TCG) in a `debian:trixie` container, as the other workflows do, on pushes to `main` and
pull requests that touch the guest, the launcher, the wire formats or the build, and by hand. The
script installs Buildroot's host tools and the `guest` and `host` build-dependency roles, builds
the test guest, the kernel and initramfs, and the host build (the prebuilt QEMU, whose surfaceless
EGL display gives virgl Mesa's software rasteriser on a runner without a GPU, and the media helper),
then runs the check in a library under `build/testguest`. The workflow caches Buildroot's downloads
and ccache and Meson's package cache, and keeps the results, the VM's logs and Buildroot's licence
manifest as the run's artifact. A macOS job is to come.

The script was run in a fresh `debian:trixie` container on the PC before the workflow's first run
(no GPU passed in, as on the runner; Buildroot's downloads cached, nothing else): packages 95 s,
Buildroot 478 s (cold ccache), kernel and initramfs 165 s, the check 23 s, about 13 min in all;
again with everything built, 58 s. The checks passed there on virgl over llvmpipe
(`gpu.renderer=virgl (LLVMPIPE (LLVM 19.1.7, 256 bits))`, OpenGL ES 3.2, `gpu.frame_ms=10.843`,
`ports.media_rtt_ms=2.974`, `host.poweroff_s=4.8`).

## Licensing

Everything in the image is built from source by Buildroot from upstream releases (BusyBox, glibc,
Mesa, libdrm, expat, zlib and the toolchain's runtime) plus our checks; no LG file, font or key.
`make legal-info` records each package's licence and source, and CI keeps the manifest; a copy of
the image passed on should come with `build/testguest/legal-info`.
