#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the open test guest: a 32-bit ARM userland made by Buildroot from this BR2_EXTERNAL tree,
# for VMs that boot without LG firmware (docs/dev/architecture/test-guest.md).
#
#   guest/testguest/build.sh [OUT]
#
# OUT (default build/testguest) holds Buildroot's source tree and output and what is published:
#   OUT/image       rootfs.pak and manifest.toml, a firmware directory for beanjar create --firmware
#   OUT/legal-info  Buildroot's make legal-info: the licences and sources of what is in the image
#   OUT/cross.ini   the cross file of this target's kernel and initramfs build (Buildroot's toolchain)
# Buildroot is the release pinned below, downloaded once and checked. BR2_DL_DIR (default OUT/dl)
# and BR2_CCACHE_DIR (default OUT/ccache) are the download and compiler caches CI keeps between runs.
set -eu

BR_VERSION=2026.02.3
BR_SHA256=5a59e7501b0b4ec52c41f4bfa79412320e0b37eae5f719605a258e8d0c6fc7fb
BR_URL=https://buildroot.org/downloads/buildroot-$BR_VERSION.tar.xz

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
out=${1:-$repo/build/testguest}
mkdir -p "$out"
out=$(cd "$out" && pwd)
export BR2_DL_DIR="${BR2_DL_DIR:-$out/dl}"
export BR2_CCACHE_DIR="${BR2_CCACHE_DIR:-$out/ccache}"
mkdir -p "$BR2_DL_DIR" "$BR2_CCACHE_DIR"

tarball=$BR2_DL_DIR/buildroot-$BR_VERSION.tar.xz
src=$out/buildroot-$BR_VERSION
if [ ! -f "$src/.unpacked" ]; then
	if ! echo "$BR_SHA256  $tarball" | sha256sum -c --status 2>/dev/null; then
		curl -sSfL -o "$tarball.part" "$BR_URL"
		echo "$BR_SHA256  $tarball.part" | sha256sum -c --quiet
		mv "$tarball.part" "$tarball"
	fi
	rm -rf "$src"
	tar -xf "$tarball" -C "$out"
	touch "$src/.unpacked"
fi
# Old Buildroot trees of an earlier pin are not needed any more
for old in "$out"/buildroot-*; do
	[ "$old" = "$src" ] || rm -rf "$old"
done

o=$out/output
make -C "$src" O="$o" BR2_EXTERNAL="$here" beanjar_testguest_defconfig >/dev/null
# Buildroot copies a local package's source once: our test programs are built afresh every time
make -C "$src" O="$o" beanjar-testguest-dirclean >/dev/null
make -C "$src" O="$o"
make -C "$src" O="$o" legal-info >/dev/null

# The image, published as a firmware directory the launcher boots in place
# (beanjar create --firmware build/testguest/image): the rootfs as its root pak and the manifest
# naming the target (guest/targets/virt-testguest.toml). Buildroot's legal-info (the licences, and
# the sources of every package in it, which a copy of the image must come with) beside it.
image=$out/image
rm -rf "$image.new"
mkdir -p "$image.new"
cp "$o/images/rootfs.squashfs" "$image.new/rootfs.pak"
cat > "$image.new/manifest.toml" <<EOF
# The Beanjar test guest, built by guest/testguest/build.sh from Buildroot $BR_VERSION: not LG firmware
name = "testguest"
chip = "virt"
release = "testguest"
EOF
rm -rf "$image.old" "$out/legal-info"
[ ! -d "$image" ] || mv "$image" "$image.old"
mv "$image.new" "$image"
rm -rf "$image.old"
cp -al "$o/legal-info" "$out/legal-info"

# The guest build of this target (meson.build, testguest) makes only the arm64 kernel and initramfs;
# its 32-bit cross compiler, which it only has to find, is Buildroot's
bin=$o/host/bin
cat > "$out/cross.ini" <<EOF
# Written by guest/testguest/build.sh:
#   meson setup --cross-file build/testguest/cross.ini build/guest-virt-testguest -Dsoc=virt -Dwebos=testguest
[binaries]
c = '$bin/arm-linux-gcc'
cpp = '$bin/arm-linux-g++'
ar = '$bin/arm-linux-ar'
strip = '$bin/arm-linux-strip'
readelf = '$bin/arm-linux-readelf'
objcopy = '$bin/arm-linux-objcopy'
nm = '$bin/arm-linux-nm'

[host_machine]
system = 'linux'
cpu_family = 'arm'
cpu = 'armv7-a'
endian = 'little'
EOF
ls -l "$image/rootfs.pak"
