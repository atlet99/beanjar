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
- another arm64 machine with KVM: KVM runs the guest kernel and only the TV's 32-bit programs are translated by qemu-user inside the guest, so it stays fast without 32-bit Arm KVM;
- an x86_64 PC, which by default emulates the whole TV, its ARM kernel included, with QEMU's TCG: it works, but more slowly. The experimental, opt-in x86-64 guest kernel (package `beanjar-guest-x86-64`) gives an x86_64 PC with KVM the qemu-user route instead, translating only the TV's programs.

On an arm64 machine, `setarch linux32 true` succeeds when it runs 32-bit code at EL0 and fails ("failed to set personality to linux32") when it does not; Beanjar asks the kernel the same question at each start and picks the route by itself.

On Windows 11, Beanjar runs in WSL2 with a Debian 13 distribution, as on an x86_64 PC; see [Running Beanjar on Windows with WSL](docs/user/wsl.md).

The Debian packages are in the [latest release](https://github.com/webosbrew/beanjar/releases/latest), the host's and the guest's together. Download and install them in a new directory (no GitHub account needed):

```sh
mkdir beanjar-debs && cd beanjar-debs
arch=$(dpkg --print-architecture)
url=https://github.com/webosbrew/beanjar/releases/latest/download
wget $url/beanjar_$arch.deb $url/beanjar-qemu_$arch.deb $url/beanjar-ui_all.deb $url/beanjar-guest_all.deb
sudo apt-get install ./beanjar_$arch.deb ./beanjar-qemu_$arch.deb ./beanjar-ui_all.deb ./beanjar-guest_all.deb
```

`beanjar-qemu` is Beanjar's patched QEMU; the qemu-user route, the x86-64 guest kernel and WSL need it. Two packages are optional; each is one more line in the same shell:

```sh
wget $url/beanjar-vnc_$arch.deb && sudo apt-get install ./beanjar-vnc_$arch.deb   # a VM's picture over VNC
wget $url/beanjar-web_all.deb && sudo apt-get install ./beanjar-web_all.deb       # VMs in a web browser
```

To try the experimental x86-64 guest kernel on an x86_64 PC with KVM, install its package (amd64 only) and switch a VM (here `tv1`, made as in [Using it](#using-it)) to it:

```sh
wget https://github.com/webosbrew/beanjar/releases/latest/download/beanjar-guest-x86-64_amd64.deb
sudo apt-get install ./beanjar-guest-x86-64_amd64.deb
beanjar settings tv1 set guest_kernel=x86-64
```

KVM (arm64 machines and the x86-64 guest kernel) needs access to `/dev/kvm`, and the GPU's render node `/dev/dri/renderD*` is used for the TV's 3D and video. Add your user to both groups, then log out and in again; `id -nG` should then list `kvm` and `render`:

```sh
sudo usermod -aG kvm,render "$USER"
```

The traffic inspector needs your own mitmproxy, 12.0.1 or newer. Beanjar finds it in `~/.local/bin`; `pipx ensurepath` and a new shell put it on your `PATH` as well:

```sh
sudo apt install pipx
pipx install mitmproxy
pipx ensurepath
```

[Check the install](docs/user/getting-started.md#check-the-install) lists commands that show everything is in place before you have firmware.

To build from source instead, see [Building](docs/dev/guides/build.md).

## Using it

Run `beanjar` to open the manager. Choose **New VM** and give it your firmware `.epk` (this needs [epk2extract](https://github.com/openlgtv/epk2extract) on your `PATH`; [Getting started](docs/user/getting-started.md#epk2extract) shows how to build it) or a directory of its extracted paks, then **Start** and **Connect**.

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
