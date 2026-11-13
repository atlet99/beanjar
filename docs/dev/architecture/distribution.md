# Debian 13 distribution layout

Targets: Raspberry Pi (arm64) and x86_64 (amd64).

## Package split

| Package | Architecture | Contents |
|---|---|---|
| `beanjar` | arm64, amd64 | Manager library and CLI, the viewer's native core and headless entry point, native media helper and host helpers, the private FFmpeg; recommends `beanjar-ui` |
| `beanjar-ui` | all | GTK manager and viewer windows, desktop entry and icon |
| `beanjar-vnc` | arm64, amd64 | VNC server for the TV picture, sound and H.264 video, with its private neatvnc and aml |
| `beanjar-web` | all | Web viewer |
| `beanjar-guest` | all | Virtual-TV kernel, initramfs, modules, guest SSH tools and built layers for supported firmware families |
| `beanjar-qemu` | arm64, amd64 | Patched QEMU |

`beanjar` depends on a guest package of the same `0.Y` (packaging/debian/README.md), a hint
for apt, plus Debian's host libraries and tools. At run time the launcher checks what the guest
payload has, not its version (build-and-boot.md, "Host and guest compatibility"). MITM is installed on demand into Beanjar's user data directory.
`beanjar` depends on `beanjar-qemu` or, as the alternative, Debian's
`qemu-system-arm` and `qemu-system-modules-opengl`. The runtime runs the patched
QEMU whenever it is installed, for every accelerator and display, and Debian's
otherwise; hosts such as WSL need the patched one for surfaceless graphics, and
every host for its D-Bus display fix.

QEMU, FFmpeg and neatvnc with aml are third-party programs Beanjar patches or needs newer than
Debian 13 has. They are not built with Beanjar: beanjar-vendor builds and releases them as
tarballs for amd64 and arm64, `packaging/vendor.toml` pins the releases, and the packages are
made from those trees (packaging/debian/README.md, "The prebuilt programs").

The guest package is architecture-independent with respect to the host: both
host architectures use the same virtual ARM TV payload.

## Build separation

Build the native host packages separately on Debian 13 arm64 and amd64.
Beanjar ships its mitmproxy addon but no mitmproxy: the inspector runs the user's own
(below).

| Build lane | Host artifacts |
|---|---|
| arm64 | `beanjar_*_arm64.deb`, `beanjar-vnc_*_arm64.deb`, `beanjar-qemu_*_arm64.deb` |
| x86_64 / amd64 | `beanjar_*_amd64.deb`, `beanjar-vnc_*_amd64.deb`, `beanjar-qemu_*_amd64.deb` |
| Shared guest build | `beanjar-guest_*_all.deb`, identical input payload for both host lanes |
| Either lane | `beanjar-ui_*_all.deb`, `beanjar-web_*_all.deb` |

The guest package's `all` designation describes host independence: its ARM guest
binaries are the same data for both host architectures. Build that payload once
and reuse it; do not assemble different guest contents under the same version.

## Repository organization

`host/core` (the core package `beanjar`), `host/ui` (`beanjar_ui`, the GTK windows and
their UI resources), `host/web` and `host/viewer` (the viewer's native core) contain the
application code.
`host/helpers/` contains native host runtime support, including media/network
helpers and firmware inspection/preparation. `guest/helpers/` contains guest
architecture programs, including the ARM LLS probe invoked through qemu-arm.
MStar mmap and Realtek RPC readers are runtime
helpers because supported launches use their derived records.
Guest kernel, initramfs and layers live under `guest/`, with guest SSH build
utility sources in `guest/tools/ssh`. Host-only scripts live in `host/scripts/`,
guest-run scripts in `guest/scripts/`, and shared build/regression entry points
remain under `scripts/`.

## Installed paths

```text
/usr/bin/
  beanjar                         CLI with commands; without, the manager GUI (beanjar-ui)
  beanjar-ui                      manager GUI
  beanjar-viewer                  viewer entry point (headless without beanjar-ui)
  beanjar-web                     web viewer

/usr/lib/beanjar/
  core/beanjar/                   the core package: VM library, CLI, host runtime
  ui/beanjar_ui/                  the GTK windows: manager and viewer
  web/beanjar_web/                the web viewer
  viewer/                         the viewer's native shared library
  ffmpeg/lib/                     the private FFmpeg (beanjar)
  neatvnc/lib/                    the private neatvnc and aml (beanjar-vnc)
  helpers/                        host Python runtime helpers and MITM addons
  installation.toml               version, commit, architecture; installed-layout marker

/usr/libexec/beanjar/
  vm-launcher                     VM launcher
  viewer-headless                 headless viewer helper
  media-helper                    native media helper
  qemu-system-aarch64             patched QEMU (beanjar-qemu)
  vhost-user-gpu                  its GPU process (beanjar-qemu)

/usr/share/beanjar/
  ui/                             compiled manager and viewer GResources
  guest/
    Image                         virtual-TV kernel
    initramfs.cpio.gz
    modules/                      virtual-TV kernel modules
    layers/common/                shared guest support
    layers/soc/<soc>/              SoC-specific components
    layers/release/<release>/      release-specific components
    layers/{gl,rootssh,...}/       optional feature components
    scripts/                      guest logging and diagnostic runtime scripts
    ssh/bin/                      guest SSH binaries
    bin/lls-probe                  guest ARM probe, invoked through qemu-arm
    boards/<soc>/board.cmdline     board inputs to RAM and EEPROM derivation
    binds.conf                    guest bind configuration
    installation.toml             guest version, commit and payload digest
  qemu/                           data of the patched QEMU (beanjar-qemu)

/usr/share/applications/
  org.webosbrew.beanjar.desktop
/usr/share/icons/hicolor/scalable/apps/
  org.webosbrew.beanjar.svg
/usr/share/doc/<package>/
  documentation and license/source notices
```

Guest executables are placed in `share` because they are data consumed by the
virtual machine, rather than programs executed natively by the host. The ARM LLS
probe also belongs in `share/beanjar/guest/bin`, even when invoked through qemu-arm.
`libexec` holds host executables and host launchers only.

Host Python helpers are executable code and belong under `/usr/lib/beanjar/helpers`,
including networking addons, firmware inspection and preparation, and SSH key
support.
The installed guest payload uses `guest/layers`, `guest/scripts` and
`guest/boards`, without repeating the `guest` directory. Assembling a VM's 9p share
keeps the guest-visible script paths it already uses (`guest/scripts/...`).

## Separate guest components and runtime composition

Publish independent component trees rather than preassembled SoC/release layers:

```text
guest/layers/
  common/
  soc/
    k5lp/
    lm21a/
  release/
    afro/
    beehive/
  gl/
  rootssh/
  hbchannel/
  headless/
  devmode/
  normal/
```

At VM start, derive the SoC and release from firmware and select enabled features
from `vm.toml`. Resolve their component directories into the VM's read-only share
and generate the ordered `boot.conf` stack. Do not publish a merged
`layers/<soc>-<release>/` tree or copy a full combination into each VM's persistent
files. Existing overlay machinery composes the selected components at boot.

Preserve precedence: VM-specific files above enabled features, release
adjustments, SoC adjustments, common support, and stock firmware. Public feature
names remain `gl`, `rootssh`, etc. The compatibility table still limits supported
combinations; separate directories do not promise arbitrary SoC/release pairing.

Each component directory contains `selections.json` and `variants/<sha256>/`.
The selector maps a supported firmware target to its compatible payload; the hash
covers file contents, paths, modes and symlink targets. Byte-identical outputs
within a component share one variant, and incompatible outputs remain explicit.
These small selectors are compatibility records, not preassembled stack profiles.
GL vendor stand-ins are under `soc/<soc>/gl`, and the Wayland server adapter is
under `release/<release>/gl`; their shared feature payload stays under `gl`.
Launch expands `gl` to those selected physical layers while preserving its public
config name and overlay order. Previous variants remain valid for running VMs
when a new build is published; package assembly includes only referenced variants.

Package scripts and configuration by their actual axis. Reusable guest binaries
must meet the ABI constraints of all firmware that selects them. Current SoC
shims use stock library names, and the GL build takes inputs from both the SoC
and release. Where a single binary cannot cover those inputs, keep explicit
variants inside its owning component and select them at launch; do not restore
full duplicated SoC/release trees. Variant selection is read-only and requires
no compiler or SDK on the installed host.

This requires separating the current combined build outputs as well as changing
publication and host layer resolution. It is not just a directory rename.
Package assembly validates component references and excludes absolute or escaping
links. Firmware remains mounted directly, without a full rootfs extraction step.

Runtime composition does not by itself remove the guest's current copying of
selected layer contents into runtime storage. Reducing that RAM use is a separate
change from eliminating preassembled package combinations.

## Package ownership

| Installed subtree | Owner |
|---|---|
| `/usr/bin/beanjar`, `/usr/bin/beanjar-viewer` | `beanjar` |
| `/usr/lib/beanjar/` but `ui/` and `web/` | `beanjar` |
| Host launchers/helpers in `/usr/libexec/beanjar/` | `beanjar` |
| `/usr/bin/beanjar-ui`, `/usr/lib/beanjar/ui/`, `/usr/share/beanjar/ui/` | `beanjar-ui` |
| `/usr/bin/beanjar-web`, `/usr/lib/beanjar/web/` | `beanjar-web` |
| `/usr/bin/beanjar-vnc` | `beanjar-vnc` |
| `/usr/share/beanjar/guest/` | `beanjar-guest` |
| `/usr/libexec/beanjar/qemu-system-aarch64`, `/usr/share/beanjar/qemu/` | `beanjar-qemu` |
| Desktop entry and application icon | `beanjar-ui` |

Each package owns its documentation and component copyright notices under
`/usr/share/doc/<package>/`. Packages may share parent directories; they do not
ship the same file. No maintainer script creates a VM library, CA or firmware cache. Installing or upgrading changes only package-owned files.

The host asks apt for a guest of the same `0.Y`, not one of its own version; the Pi updates
its host alone while the minor stays the same. The optional QEMU package
has its own dependency closure and is built separately; it is not folded into the
host package when a patched build happens to be present. It contains no ARM guest
payload. Ordinary QEMU remains supplied by Debian.

Installed paths should be explicit in the runtime's path abstraction. Checkout
runs retain their source/build paths. An installed package should not need a
replica checkout tree or writable files below `/usr`.

## User data and caches

- Existing libraries remain in `$XDG_DATA_HOME/beanjar` (normally
  `~/.local/share/beanjar`) or the user's configured library directory.
- Firmware, VM configs, identities, state and logs remain user-owned.
- Checkout development VMs use separate managed development libraries under
  the same user data base, with their own CA. `migrate-local` copies legacy keys
  and sparse state offline and retains the originals; runtime defaults no longer
  create identity files in the source checkout. Firmware lives in the host's
  firmware collections (`firmware_dirs` in the machine's `config.toml`, default
  `/srv/beanjar/firmware`, possibly a read-only share), used in place; temporary
  experiments under `scratch/firmware/` or explicit external paths; no
  checkout-root `firmware/` directory is part of the runtime layout.
- Generated LLS data uses `$XDG_CACHE_HOME/beanjar/lls` (normally
  `~/.cache/beanjar/lls`). No firmware-derived cache is packaged.
- Per-VM temporary runtime files remain in the existing VM runtime directory.

## Dependencies and installation behavior

Use Debian packages for GTK, libadwaita, SDL, GL, networking and ordinary QEMU; FFmpeg,
neatvnc and the patched QEMU are the prebuilt ones above. Derive native library dependencies
from the final ELF payloads, and those of the prebuilt trees from their `BUILDINFO`.

mitmproxy is the user's own, 12.0.1 or newer, installed however they like; the documented way is
`pipx install mitmproxy`, since Debian 13 has no package of it. The packages neither ship nor depend
on it (pipx cannot be a `Suggests`), and no maintainer script installs it. `mitmtool.py` finds it
(`NETNS_MITMWEB`, `PATH`, `~/.local/bin`) and reads its version at each start. Beanjar runs its
`mitmweb` executable, so mitmproxy's own interpreter loads its dependencies and the addon; nothing
of it is imported into the manager's Python. Without a usable one a mitm VM starts with direct
networking and says why; direct and off networking need none.

Installation does not convert VM configs, start VMs or change device permissions.

Firmware SquashFS images are attached and mounted directly by the guest. Host
inspection reads selected files for firmware detection and EEPROM preparation;
it does not require unpacking a complete root filesystem. Optional uncompressed
caching produces another SquashFS image, without a directory extraction.
Use Debian's official squashfs-tools for inspection, extraction and conversion.
Stock symlink behavior is accepted; do not maintain a custom filesystem parser
or symlink resolver. Uncompressed conversion uses pseudo-file export/import
(squashfs-tools 4.6 or later), preserving metadata and verifying file contents.

Exclude SDP research, key calculation and provisioning CLIs from the distribution.
The manager retains only validation and installation of user-supplied keys.
No model keys or firmware-derived identity material are packaged.

Users supply extracted firmware. Running packages requires neither a source
checkout nor a webOS SDK. Importing EPK archives additionally requires the user's
configured `epk2extract`.

## Design record

Host native builds are independent
arm64/amd64 lanes; the shared ARM guest input is reused unchanged. Device
validation uses installed Debian packages, not a synchronized source checkout.
mitmproxy is not Beanjar's to install: the user's own, 12.0.1 or newer. Component variants
are selected from compatibility metadata; no full target stack is packaged.

Public release licensing and source-distribution work is tracked in the issue;
this document records the runtime and package structure.
