# Getting started

Beanjar runs LG TV firmware that you supply in QEMU VMs. This page covers what a host needs, the
two ways to run Beanjar, and the firmware it takes. [Creating and running VMs](vms.md) follows on
from here.

## Requirements

- Debian 13 (trixie) on the host.
- An x86_64 machine, which runs the guest under QEMU's TCG, or an arm64 machine with AArch32 at
  EL0, which runs it under KVM (a Raspberry Pi 5 does). The firmware's userland is 32-bit ARM, so
  an arm64 CPU without AArch32 at EL0 cannot run it under KVM.
- Memory for each VM: its firmware model's RAM (1.25 GB for WT1H, 1.5 GB for W19P, for example)
  plus 1 GiB of headroom by default, and under TCG another 1 GiB for QEMU's translation cache. A
  start that the host's free memory does not cover is refused.
- For the windows (the VM manager and the viewer): GTK 4 and libadwaita 1.7 or newer for Python.
  Without them the CLI, VNC and the web viewer still work.
- The guest layers are built with the webOS SDK (`/opt/arm-webos-linux-gnueabi_sdk-buildroot`),
  which is x86_64 only, so they are built on an x86_64 machine; NetCast 4.5 needs the
  [lgtv SDK](https://github.com/openlgtv/buildroot-nc4) as well.

## Firmware

No firmware, keys or model secrets come with Beanjar. You supply an LG firmware file (`.epk`) or a
directory of its extracted paks, for one of the firmware builds in the
[support table](../../README.md#supported-firmware). Other models are refused when the VM is made.

- The manager window's **New VM** takes a `.epk` and extracts it with
  [epk2extract](https://github.com/openlgtv/epk2extract), which must be on `PATH` (or in
  `~/.local/bin`).
- The CLI takes extracted paks: `beanjar create NAME --firmware DIR --import` makes them the VM's
  own (hard links where the directory is on the same filesystem, else a copy).
- A firmware shared by several VMs or hosts can stay in a firmware collection and be used in
  place: each firmware is a directory of paks with a `manifest.toml` naming it, under
  `/srv/beanjar/firmware` by default (`beanjar host config set firmware_dirs=DIR[:DIR...]`).
  See [Firmware](../../host/core/README.md#firmware) in the manager guide.

## From a checkout

Install the host build's packages ([Host packages](../dev/guides/build.md#host-packages)), then
build the host programs, and on x86_64 the guest layers for your firmware's target:

```sh
scripts/build.sh host
FW=/path/to/extracted-paks scripts/build.sh guest
```

NetCast 4.5 needs its own guest build directory set up first; see
[Commands](../dev/guides/build.md#commands). An arm64 host builds only the host side and takes the
guest build from the x86_64 machine (the published `build/` directory, or the `beanjar-guest`
package).

Run the CLI and the manager window from the checkout:

```sh
export PYTHONPATH=host/core:host/ui
python3 -m beanjar --help
python3 -m beanjar_ui.manager
```

The rest of these pages write `beanjar`; from a checkout that is `python3 -m beanjar`. The host
build also makes the mitmproxy environment that a VM's default network uses.

## From the Debian packages

No packages are published yet; they are built from a checkout after the host and guest builds:

```sh
python3 packaging/debian/build.py
sudo apt install ./build/packages/beanjar-guest_<version>_all.deb \
  ./build/packages/beanjar_<version>_$(dpkg --print-architecture).deb \
  ./build/packages/beanjar-ui_<version>_all.deb
```

`beanjar-vnc` (the TV picture over VNC) and `beanjar-web` (VMs in a browser) are optional, and so
is `beanjar-qemu`, the patched QEMU (without it the VMs run on Debian's); see the
[packages guide](../../packaging/debian/README.md) for the packages.

The packages install the commands `beanjar` (the CLI; without arguments it opens the manager
window), `beanjar-ui` (the manager window), `beanjar-viewer`, and with the optional packages
`beanjar-vnc` and `beanjar-web`. A VM's default network records its web traffic through
mitmproxy, which an installed Beanjar downloads into your user data once, when you agree:

```sh
beanjar mitm setup --install
```

A VM with `mode=direct` (see [network](vms.md#network)) does not need it.

## Where things are kept

VMs live in the library, `~/.local/share/beanjar` by default (`--library DIR` or
`BEANJAR_LIBRARY` for another): one directory per VM under `vms/`, with its settings (`vm.toml`),
its state disks, its identity and its logs. This machine's own settings are in
`~/.config/beanjar/config.toml` (`beanjar host config`). The [manager guide](../../host/core/README.md#library)
lists every file.
