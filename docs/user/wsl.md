# Running Beanjar on Windows with WSL

Beanjar runs on Windows 11 inside WSL2, with a Debian 13 (trixie) distribution, as on any x86_64
Debian host: install it as [Getting started](getting-started.md) says. This page covers what is
different under WSL.

## The CPU and KVM

An x86_64 PC runs the TV under QEMU's TCG by default, which needs no KVM. `/dev/kvm` matters only
for the experimental `guest_kernel = "x86-64"` setting (see the
[requirements](getting-started.md#requirements)), which uses KVM when `/dev/kvm` opens and falls
back to TCG otherwise.

WSL2 gives its distributions `/dev/kvm` through nested virtualisation, which is on by default on
Windows 11 (`nestedVirtualization` in the `[wsl2]` section of `%UserProfile%\.wslconfig`; set it to
`true` if it was turned off, then run `wsl --shutdown`). Check from Debian:

```sh
ls -l /dev/kvm        # crw-rw---- root kvm
```

## Groups

Add your user to `kvm` (for `/dev/kvm`) and `render` (for vgem's render node, below), then start
a new WSL session (`wsl --terminate <distribution>` from Windows, or `wsl --shutdown`):

```sh
sudo usermod -aG kvm,render "$USER"
```

## The GPU

WSL has no GPU render node in `/dev/dri`. The Windows GPU is reached through `/dev/dxg`, which is
open to every user, and Mesa's d3d12 driver, which Debian 13's Mesa includes, with the Windows
driver's libraries in `/usr/lib/wsl/lib`. On such a host Beanjar draws the guest's 3D with virgl on
a surfaceless EGL display and sets `GALLIUM_DRIVER=d3d12` for QEMU, the viewer and `beanjar-vnc`
unless you set it yourself (without it Mesa falls back to llvmpipe, the CPU). This needs Beanjar's
patched QEMU, the `beanjar-qemu` package (or `scripts/build.sh host` in a checkout): Debian's QEMU
does not render without a render node. Check that d3d12 reaches your GPU:

```sh
sudo apt install mesa-utils
GALLIUM_DRIVER=d3d12 eglinfo -B | grep renderer    # D3D12 (<your GPU>)
```

`beanjar-vnc`'s VA-API video encoder (`--video-encoder vaapi`) also needs a render node, which the
`vgem` module provides: `sudo modprobe vgem`, or `vgem` in a file under `/etc/modules-load.d/` to
load it at every start. Beanjar never gives vgem's node to the guest's 3D, since it has no GPU
behind it.

## Windows

The manager window and the viewer open as ordinary Windows windows through WSLg, which Windows 11's
WSL includes, and the viewer's sound plays through WSLg's PulseAudio server. Nothing needs setting
up for that.

## systemd

Beanjar runs each VM as a transient systemd user service when a user systemd is there; without
one, a VM runs in a session of its own instead. To have systemd in WSL, add this to
`/etc/wsl.conf` and restart WSL:

```ini
[boot]
systemd=true
```
