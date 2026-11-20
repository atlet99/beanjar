# Installing the macOS stack with Homebrew

There is no macOS app release yet. Developers can run the open test guest in the foreground on
Apple silicon using HVF, with networking off or direct NAT. The open test guest also supports
QEMU's native Cocoa window; TV firmware graphics and sound are not supported in this preview.
The tap installs only dependencies. Its bottles for Apple silicon on macOS 15 are released on
[beanjar-vendor](https://github.com/mariotaku/beanjar-vendor/releases).

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

A Mac with Apple silicon (Intel Macs are not supported), macOS 15 or newer and
[Homebrew](https://brew.sh):

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
formula from source, which for `beanjar-angle` needs Xcode and takes a while.

## Developer smoke test: boot the open guest

Follow the [test guest build instructions](../dev/architecture/test-guest.md) on Linux and copy
the published `build/Image`, `build/initramfs.cpio.gz`, `build/lgemu.ko`, `build/components/`
and `build/testguest/image/` into the same paths in the Mac checkout. Put the pinned Linux ARM64
`qemu-arm` from [`packaging/vendor.toml`](../../packaging/vendor.toml) at
`build/qemu-user/bin/qemu-arm`. This interpreter runs **inside** the guest; the Mac's host QEMU is
a different executable. For the smoke test you can use Homebrew's upstream QEMU and SquashFS
tools (`brew install qemu squashfs`). You also need Python 3.11 or newer.

From the Mac checkout, use a short temporary path for the Unix sockets and a fresh identity:

```sh
run=$(mktemp -d /tmp/beanjar-hvf.XXXXXX)
printf 'Runtime: %s\n' "$run"
QEMU="$(command -v qemu-system-aarch64)" \
  ACCEL=hvf NET_LINK=off STATE=none SLOT=1 TMPDIR=/tmp \
  FW="$PWD/build/testguest/image" RUN_DIR="$run" IDENTITY_DIR="$run/identity" \
  LOG_DIR="$run" host/scripts/vm-launcher run
```

At the `[initramfs ~]#` prompt, verify that a 32-bit shell can write to the host output share:

```sh
webos-enter /bin/sh -c 'echo arm32-ok > /mnt/host/out/hvf-smoke'
```

In another terminal, substitute the printed runtime path, check the file and query QEMU:

```sh
run=/tmp/beanjar-hvf.XXXXXX
test "$(cat "$run/share/out/hvf-smoke")" = arm32-ok
QMP_SOCK="$run/qmp.sock" host/scripts/vm-launcher qmp '{"execute":"query-status"}'
QMP_SOCK="$run/qmp.sock" host/scripts/vm-launcher qmp '{"execute":"quit"}'
```

Expect `running` before `quit`, then the foreground launcher to exit. This smoke test was run on
macOS 26.3.1 with Homebrew's upstream QEMU 11.1.3. The full guest test suite, real TV firmware and
the desktop viewer are outside this smoke test. HVF does not expose AArch32 on Apple silicon, so
the guest uses `qemu-user`; it has extra RAM but no Linux memfd-backed swap.

If early systemd services fail with `237/KEYRING` under HVF, repeat the headless launch with
`ACCEL=tcg LAYERS=headless`. TCG runs the 32-bit firmware without `qemu-user`, so it is useful as
a compatibility baseline, though it is slower than HVF. Keep `NET_LINK=off` for this comparison.

## Developer smoke test: direct NAT

For a networked run, replace `NET_LINK=off` with `NET_LINK=nat NET_MODE=direct`. Direct mode allows
outbound connections without the traffic inspector. To reach a guest service from the Mac, add
`FORWARD=18080:8080` and start a service on guest port `8080`. Connect from the Mac to
`127.0.0.1:18080`. Forwarded ports default to `127.0.0.1` on macOS, so use distinct host ports
for concurrent VMs. `LISTEN_ADDRESS` overrides that bind address; `0.0.0.0` exposes the ports on
all of the Mac's interfaces.

For a local outbound check, run `python3 -m http.server 18081 --bind 127.0.0.1` on the Mac in an
empty directory with a test `index.html`. In the test guest's initramfs console, configure its
interface and fetch the file through QEMU's host-loopback address:

```sh
webos-enter /bin/sh -c 'ip link set eth0 up && ip addr add 10.0.2.15/24 dev eth0 && wget -O - http://10.0.2.2:18081/'
```

## Building a CLI disk image without Developer ID

With the guest artifacts described above, the published guest components and LLS/SSH artifacts
in `build/`, and Homebrew's `uv`, `qemu`, `squashfs`, `e2fsprogs`, `openssl@3` and `imagemagick` installed:

```sh
python3 packaging/macos/build.py
shasum -a 256 "build/Beanjar-$(tr -d '\n' < packaging/VERSION)-macos-arm64-preview-adhoc.dmg"
```

The default build signs the Apple Silicon executables and libraries ad hoc, including the
Hypervisor entitlement for QEMU. The versioned DMG contains `Beanjar.app`, an `Applications`
shortcut and installation instructions. The app targets macOS 26 on Apple Silicon. Opening it from
Finder starts the open test guest, with a native QEMU window and diagnostics in Terminal. The
window shows a boot screen drawn by the guest framebuffer, not the webOS TV interface.

The TV graphics path needs virgl and a working EGL display on macOS. The current patched QEMU
bottle still fails with `egl: not available on this platform` for both `egl-headless` and
`dbus,gl=on`. Its surfaceless entry point is currently selected only on GBM builds. Cocoa with a
2D virtio GPU does not provide the TV compositor's GL path.

To use your own Keychain identity and sign the image itself as well:

```sh
security find-identity -v -p codesigning
python3 packaging/macos/build.py --identity '<exact Keychain identity name or SHA-1 hash>'
```

This writes the same versioned filename without `-adhoc`. If certificates share a name, use the SHA-1
hash to select one. Neither an ad-hoc signature nor a self-signed certificate provides Apple
notarization. Publish the SHA-256 alongside downloads; an ad-hoc signature does not identify
the publisher.

## Updating and removing

`brew upgrade` updates them with everything else. `brew uninstall beanjar-qemu beanjar-virglrenderer
beanjar-libepoxy beanjar-angle` and `brew untap webosbrew/beanjar` remove them.
