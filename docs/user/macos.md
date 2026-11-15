# Installing the macOS stack with Homebrew

**Not usable yet.** Beanjar does not run VMs on macOS yet, the tap below becomes reachable only
once Beanjar's repository is public, and its bottles (prebuilt binaries) are not released. Until
then the formulae are built and tested only in beanjar-vendor's CI. This page says how it will
work.

On a Mac, the programs Beanjar's VMs need from outside Beanjar come from a Homebrew tap: Beanjar's
patched QEMU and the OpenGL ES stack it draws with (ANGLE on Metal, libepoxy and virglrenderer).
They use Homebrew's own libraries (GLib, pixman, libslirp and so on) rather than bundling them.

| Formula | What |
|---|---|
| `beanjar-qemu` | QEMU for the virtual TV (`qemu-system-aarch64`), with Beanjar's display patches |
| `beanjar-virglrenderer` | 3D for the guest, on OpenGL ES |
| `beanjar-libepoxy` | OpenGL ES and EGL loading for QEMU and virglrenderer |
| `beanjar-angle` | OpenGL ES and EGL on Metal |

## Installing

macOS 15 or newer, on Apple silicon or Intel, with [Homebrew](https://brew.sh):

```sh
brew tap webosbrew/beanjar https://github.com/webosbrew/beanjar
brew trust webosbrew/beanjar
brew install beanjar-qemu
```

`brew trust` tells Homebrew to use formulae from this tap by their short names; Homebrew asks it
of every tap that is not its own. Installing `beanjar-qemu` installs the other three with it.

All four are keg-only: they are not linked into Homebrew's `bin` or `lib`, so they never clash
with Homebrew's own `qemu`, `libepoxy` or `virglrenderer`, which can be installed beside them.
Beanjar will run its QEMU from `$(brew --prefix beanjar-qemu)/bin/qemu-system-aarch64`.

Where a bottle exists for your macOS and processor, Homebrew downloads it; otherwise it builds the
formula from source, which for `beanjar-angle` needs Xcode and takes a while. On Intel Macs
Homebrew itself no longer has bottles (it supports them only at its lowest tier), so there it
builds Homebrew's own dependencies (Python, GLib and the rest) from source too.

## Updating and removing

`brew upgrade` updates them with everything else. `brew uninstall beanjar-qemu beanjar-virglrenderer
beanjar-libepoxy beanjar-angle` and `brew untap webosbrew/beanjar` remove them.
