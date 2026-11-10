#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The host's QEMU, built wherever its dependencies are (host/tools/build/qemu/meson.build decides; the GPU path on WSL2 is):
# QEMU with host/tools/build/qemu/patches/ (a surfaceless EGL display, D-Bus read-back of GL
# scanouts, dropping a D-Bus listener whose connection closed) linked
# against Debian 13's virglrenderer (1.1.0; >= 1.0 works on Mesa's d3d12 driver), libslirp and
# libfdt. host/tools/build/qemu/meson.build runs it:
#
#   host/tools/build/qemu/build.sh SRC OBJ STAGE STAMP
#
# SRC is the unpacked, patched QEMU (subprojects/qemu.wrap); OBJ its build directory (incremental);
# STAGE receives the installed tree: bin/qemu-system-aarch64, libexec/vhost-user-gpu, share/qemu
# (published as build/qemu).
# STAMP is rewritten only when STAGE's contents change.
#
#   host/tools/build/qemu/build.sh SRC OBJ STAGE STAMP CROSS_PREFIX PKG_CONFIG RUN
#
# A cross build (the arm64 host build on the PC, meson/cross/debian-arm64.ini) adds what its cross
# file gives: configure's --cross-prefix (aarch64-linux-gnu-), the target's pkg-config, and the
# qemu-user (qemu-aarch64) that runs the staged binary's --version check.
#
# Only aarch64-softmmu is built, with what the launcher (host/core/beanjar/qemu.py) uses:
# OpenGL/virglrenderer for virtio-gpu-gl, egl-headless and the D-Bus display (+ its audio
# backend; -audiodev none is built in), GBM, slirp, virtio-9p, virtio-sound, pixman, PNG (QMP
# screendump, host/scripts/vm-launcher qmp), and usb-redir (libusbredirparser) and usb-host (libusb)
# to forward a real USB device to the VM's qemu-xhci (usb-redir's plain TCP is carried over SSH).
# Of the tools only QEMU's own vhost-user-gpu backend (contrib/vhost-user-gpu) is kept: on an ARM host
# with a render node the launcher runs the guest's virgl GPU in that process. Its libraries
# are qemu-system-aarch64's (virglrenderer, epoxy, GBM, pixman, GLib); configure's tools option is
# the only switch that builds it, so the other tools are built and left out of STAGE.
# No SDL/GTK/VNC/SPICE/curses, no host audio backends, no block
# drivers or crypto libraries beyond QEMU's built-in ones. Nothing is downloaded: the QEMU
# tarball carries the subprojects that remain (keycodemapdb, berkeley-softfloat-3).
# Build dependencies are Debian 13 packages (docs/dev/guides/build.md, "Host packages"): ninja-build,
# pkg-config, libglib2.0-dev, libpixman-1-dev, libepoxy-dev, libgbm-dev, libdrm-dev, libpng-dev,
# zlib1g-dev, libvirglrenderer-dev, libslirp-dev, libfdt-dev, libusbredirparser-dev,
# libusb-1.0-0-dev, flex, bison,
# python3-venv, python3-pycotap (QEMU's configure venv takes the system Meson and pycotap).
set -eu
SRC=$1 OBJ=$2 STAGE=$3 STAMP=$4 CROSS_PREFIX=${5:-} CROSS_PKG_CONFIG=${6:-} RUN=${7:-}
HERE=$(cd "$(dirname "$0")" && pwd)
set -- --prefix=/qemu --target-list=aarch64-softmmu --disable-download \
	--disable-modules --disable-docs --disable-user --enable-tools --disable-guest-agent \
	--enable-opengl --enable-virglrenderer --enable-dbus-display \
	--enable-slirp --enable-fdt=system --enable-virtfs --enable-attr --enable-pixman --enable-png \
	--disable-sdl --disable-gtk --disable-vnc --disable-spice --disable-xen --disable-curses \
	--audio-drv-list= --disable-alsa --disable-pa --disable-pipewire --disable-jack \
	--disable-sndio --disable-oss \
	--disable-curl --disable-gnutls --disable-nettle --disable-gcrypt --disable-capstone \
	--disable-libudev --disable-numa --disable-libdw --disable-selinux \
	--disable-lzo --disable-bzip2 --disable-zstd --enable-libusb --enable-usb-redir \
	--disable-smartcard --disable-libssh --disable-seccomp --disable-libvduse --disable-vduse-blk-export
if [ -n "$CROSS_PREFIX" ]; then
	set -- "$@" --cross-prefix="$CROSS_PREFIX"
	export PKG_CONFIG="$CROSS_PKG_CONFIG"
fi
mkdir -p "$OBJ"
OBJ=$(cd "$OBJ" && pwd)
key="$SRC $*"
if [ ! -f "$OBJ/build.ninja" ] || [ "$(cat "$OBJ/.configured" 2>/dev/null)" != "$key" ]; then
	rm -rf "$OBJ"; mkdir -p "$OBJ"
	(cd "$OBJ" && "$SRC/configure" "$@" >configure.log 2>&1) ||
		{ tail -n 30 "$OBJ/configure.log" >&2; exit 1; }
	echo "$key" > "$OBJ/.configured"
fi
make -C "$OBJ" -j"$(nproc)" >"$OBJ/make.log" 2>&1 || { tail -n 30 "$OBJ/make.log" >&2; exit 1; }
rm -rf "$STAGE.tmp"
make -C "$OBJ" install DESTDIR="$STAGE.tmp" >/dev/null
rm -rf "$STAGE"
mv "$STAGE.tmp/qemu" "$STAGE"
rm -rf "$STAGE.tmp"
# what the tools option builds besides the backend (qemu-img, qemu-nbd, the helpers), and the
# backend's vhost-user JSON, which names configure's prefix rather than where it is installed
find "$STAGE/bin" "$STAGE/libexec" -type f ! -name qemu-system-aarch64 ! -name vhost-user-gpu -delete
rm -rf "$STAGE/share/qemu/vhost-user"
test -x "$STAGE/libexec/vhost-user-gpu"
# the staged binary runs (a failure stops the build)
version=$(${RUN:+"$RUN"} "$STAGE/bin/qemu-system-aarch64" --version)
echo "$version" | head -n 1 >&2
${RUN:+"$RUN"} "$STAGE/libexec/vhost-user-gpu" --print-capabilities >/dev/null
python3 "$HERE/../../../../meson/stage.py" --manifest "$STAGE" "$STAMP"
