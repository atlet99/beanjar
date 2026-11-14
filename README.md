<img src="host/ui/data/icon.svg" width="96" alt="Beanjar icon">

# Beanjar

Beanjar runs LG TV firmware in virtual machines on your computer. Give it a firmware file you have, and it boots LG's own software to the TV's Home screen in a window, with an on-screen remote. Each virtual TV keeps its own firmware, settings, state and identity, and several can run side by side.

![Beanjar viewer showing a webOS 1 Home screen beside the on-screen remote](docs/assets/screenshots/beanjar-viewer.png)

## What it is for

- **Testing your apps** (web apps, IPKs, homebrew) on many webOS versions without owning a TV of each year, through Developer Mode and LG's `ares-*` tools, or Homebrew Channel and root access as on a rooted TV.
- **Tinkering without bricking anything.** The firmware is only ever read; what the TV writes lives on the VM's own disks. Wipe a VM back to a factory-fresh TV, clone one before an experiment, or give it a new identity.
- **Seeing what the TV sends.** A traffic inspector records every connection the TV makes.
- **Research and preservation** of older firmware, after the TVs that shipped it are gone.

## What it is not

- **Not for piracy, and it can't be used for it.** No LG firmware, apps, keys or content come with Beanjar; you supply firmware you obtained yourself. Protected video and DRM are out of scope, and LG's services will not work on the emulator because we don't provide the keys.
- **Not identical to a real TV.** We try hard to match the real set, but a VM can behave differently. The kernel is not LG's stock kernel, there is no real broadcast reception or Wi-Fi, and some hardware is only approximated. Test on a real TV before you ship.

Beanjar is not affiliated with LG Electronics; LG, webOS and NetCast are LG's trademarks.

## Supported firmware

Beanjar runs LG webOS from version 1 onwards, plus the older NetCast 4.5 and SimpleSmart platforms. Each firmware build maps to a target; other models are refused when a VM is made. The current list is in [`guest/targets/`](guest/targets/), described in [Firmware targets](docs/dev/architecture/firmware-targets.md).

## Install

You need Debian 13 (trixie) or Raspberry Pi OS based on it, on one of:

- a Raspberry Pi 5, or another arm64 machine that runs 32-bit code at EL0 (AArch32), which run the TV under KVM;
- an x86_64 PC, which emulates the TV's ARM CPU with QEMU's TCG: it works, but more slowly.

Download the Debian packages from the [releases page](https://github.com/mariotaku/beanjar/releases): the host packages from the newest `host-<version>` release, and `beanjar-guest` from the `guest-<version>` release its notes name. Then:

```sh
sudo apt install ./beanjar_*.deb ./beanjar-qemu_*.deb ./beanjar-ui_*.deb ./beanjar-guest_*.deb
```

`beanjar-vnc` (a VM's picture over VNC) and `beanjar-web` (VMs in a web browser) are optional. To use KVM and the GPU, add your user to the `kvm` and `render` groups. The traffic inspector needs your own mitmproxy, 12.0.1 or newer:

```sh
sudo apt install pipx
pipx install mitmproxy
```

To build from source instead, see [Building](docs/dev/guides/build.md).

## Using it

Run `beanjar` to open the manager. Choose **New VM** and give it your firmware `.epk` (this needs [epk2extract](https://github.com/openlgtv/epk2extract) on your `PATH`) or a directory of its extracted paks, then **Start** and **Connect**.

Or from a terminal:

```sh
beanjar create tv1 --firmware /path/to/firmware.epk
beanjar start tv1
beanjar viewer open tv1 --remote
beanjar stop tv1
```

`beanjar --help` lists every command.

## Documentation

- [Using Beanjar](docs/user/README.md): getting started, VMs, the viewer and remote, installing apps, logs and the traffic inspector.
- [The VM manager](host/core/README.md): every command and setting.
- [Developing Beanjar](docs/dev/README.md) and [Contributing](.github/CONTRIBUTING.md).

## Licence

Beanjar is free software under the GNU General Public License, version 2 or (at your option) any later version (GPL-2.0-or-later); see [LICENSE](LICENSE).
