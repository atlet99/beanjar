# neatvnc 1.0 for Debian trixie

beanjar-vnc builds against neatvnc 1.0 ([vnc architecture](../architecture/vnc.md)). Debian
trixie has only 0.9.1, whose RSA-AES sends the server's challenge without the ciphertext's leading
zero bytes, so about one login in 256 fails (1.0.0's 1f6cd6b sends it at the key's length
and also closes a pre-auth stack overflow with a large client key). Debian's 1.0.1 (forky, sid)
is built against FFmpeg 9 and a newer libc and does not install on trixie, and nothing is in
trixie-backports. `scripts/neatvnc-local/` therefore builds neatvnc 1.0 and the aml 1.0 it needs
for trixie, amd64 and arm64, from Debian's own packaging, as local packages with Debian's names
and sonames (`libneatvnc1`, `libneatvnc-dev`, `libaml1`, `libaml-dev`). Local use only; the
packages are not published and not part of Beanjar's.

The same build carries two fixes for V4L2 H.264 encoders: beanjar-vnc hands its composed frames
to neatvnc as GBM buffers on hosts with one ("Frames on RK3399"), so VNC clients that ask for Open
H.264 (noVNC first of all) get the RK3399's hardware encoder (the hantro-vpu DKMS module, a
separate project), and two of neatvnc's behaviours there contradict the V4L2 specification.

## Recipe

- aml: Debian's `aml` 1.0.0-1 (forky), fetched with `apt-get source` (signed archive), unchanged.
- neatvnc: upstream 1.0.4 (2026-11-01, GitHub's tag tarball, pinned by SHA-256) with Debian's
  `neatvnc` 1.0.1+dfsg-1 packaging (forky, `apt-get source`), repacked without debian/copyright's
  `Files-Excluded` as Debian's `+dfsg` is. 1.0.4 rather than Debian's 1.0.1: it carries upstream's
  "Use poll() instead of map to wait" for the V4L2 encoder (d969d7e, master's d6ee55d, which the
  0.9.1 build carried as a backport) and later fixes (WebSocket header bounds checks, the client's
  bits per pixel validated, the stream ordering fixes of 1.0.2 and 1.0.3). Its `meson.build`
  needs nothing Debian's 1.0.1 packaging does not install.
- Built against trixie's own libraries (FFmpeg 7.1, GnuTLS, libc 2.41).
- `patches/`, applied as `debian/patches` (quilt), both to `src/enc/h264/v4l2m2m-impl.c` and not
  upstream (checked 2026-11-09, 1.0.4 and master):
  - `01-v4l2m2m-drm-fourcc.patch`: the DRM fourcc is tried as the V4L2 format first. DRM and
    V4L2 share fourcc codes for the same memory layout (DRM `XRGB8888`, `XR24`, bytes B, G, R,
    X, is V4L2 `XBGR32`); the name-based table asked for V4L2 `XRGB32` (`BX24`, X, R, G, B), so a
    driver that follows the specification swapped red and blue.
  - `02-v4l2m2m-constant-quality.patch`: `open-h264.c` passes a QP-like quality (1 best, 51
    worst) straight into `V4L2_CID_MPEG_VIDEO_CONSTANT_QUALITY`, whose range is 1 to 100 with 100
    best; mapped as `100 - (q - 1) * 99 / 50`. The QP is held at 18 or better (the client's
    level 6, V4L2 value 67): noVNC's default level 3 (QP 34, value 35) leaves faint vertical
    streaks in flat H.264 areas, and the level also sets Tight's JPEG quality, which the viewer
    should keep low, so the floor is in the H.264 encoder only. Levels 6 to 9 are unchanged. The
    floor is chosen from the level-to-QP arithmetic, not yet seen on a board.
  Both were written for 0.9.1 and apply to 1.0.4 unchanged but for line offsets.
- Versions `1.0.0-1~bpo13+beanjar1` (aml) and `1.0.4+dfsg-0~bpo13+beanjar2` (neatvnc; `-0`: no
  Debian revision of 1.0.4 exists; `beanjar2`: the shlibs change below). `~bpo13` sorts below the Debian version it comes from, as a
  backport does, so a Debian 1.0.4 that reaches trixie (a backport or an update) supersedes it;
  that one would lack the two patches, which only the RK3399 board needs.
- `libneatvnc1`'s shlibs ask for `libneatvnc1 (>= 1.0.0)` (`dh_makeshlibs -V`, added to
  `debian/rules`), not dh_makeshlibs' default of the build's own `(>= 1.0.4+dfsg)`: Raspberry Pi
  OS's `1.0.4-1+rpt1` sorts below that, so an arm64 beanjar-vnc cross-built on the PC would not
  install on the Pi. 1.0.1 to 1.0.4 changed no public header (`include/neatvnc.h`).
- amd64 is built natively and runs neatvnc's tests; arm64 is cross-built, `nocheck`. Both in one
  chroot: aml first, installed there, then neatvnc.

## Build

On the PC, from a checkout (needs sudo and mmdebstrap):

```
scripts/neatvnc-local/build.sh
```

The script starts itself as the transient unit `neatvnc-local-build` and builds in a throwaway
mmdebstrap chroot under `scratch/neatvnc-local/` (removed when it ends), in about five minutes. The
`.debs` and `build.log` land in `scratch/neatvnc-local/out/`. `ARCHS=amd64` builds for the PC
only.

## Install

beanjar-vnc's package depends on `libneatvnc1 (>= 1.0.0)` and `libaml1 (>= 1.0.0)`
(dpkg-shlibdeps), and `beanjar-build-deps-host` and `-host-arm64` on `libneatvnc-dev (>= 1.0)`
and `libaml-dev (>= 1.0)`. `libneatvnc1` and `libaml1` co-install with trixie's `libneatvnc0` and
`libaml0t64` (Weston's VNC backend keeps those); the -dev packages replace trixie's.

- The PC (amd64, and arm64 multiarch for the cross build), from `scratch/neatvnc-local/out/`:
  `sudo apt install ./libaml1_*_amd64.deb ./libaml-dev_*_amd64.deb ./libneatvnc1_*_amd64.deb
  ./libneatvnc-dev_*_amd64.deb` and the same four `_arm64.deb`, then rebuild `build/host` and
  `build/host-arm64` (Meson reconfigures for the new version).
- The ROCK Pi 4 boards boot a shared read-only base: install
  `libaml1` and `libneatvnc1` `_arm64.deb` into the base on the Pi (`systemd-nspawn ... apt-get
  install`), with a beanjar-vnc built against them, then reboot the boards. The board's old
  `libneatvnc0` (the 0.9.1 local build) is no longer used and may be removed.
- The Pi needs none of these: Raspberry Pi OS has its own `libneatvnc1` (1.0.4-1+rpt1 in its
  archive), which already has the key-padding fix and the poll() change, and the Pi 5 has no H.264
  encoder for the two patches to matter.
