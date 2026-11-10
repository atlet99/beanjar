#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The arm64 side of the guest, built with its own Kbuild/make (guest/kernel/meson.build runs this):
#
#   kbuild.sh linux   SRC OBJ CROSS_COMPILE DEFCONFIG OUT CPIO MODULES
#                                                          DEFCONFIG -> arch/arm64/boot/Image,
#                                                          the tree's usr/gen_init_cpio (a host
#                                                          program: the initramfs is made with it),
#                                                          and MODULES, the kernel's own modules as
#                                                          modules_install lays them out
#                                                          (<release>/kernel/..., modules.dep, ...)
#   kbuild.sh busybox SRC OBJ CROSS_COMPILE CONFIG OUT     CONFIG -> static busybox
#   kbuild.sh module  SRC OBJ CROSS_COMPILE MSRC MOBJ OUT [SYMVERS...]
#                                                          the external module in MSRC (its
#                                                          Kbuild), built in MOBJ against the
#                                                          kernel built in OBJ -> OUT (<name>.ko);
#                                                          SYMVERS: the Module.symvers of the
#                                                          modules whose exports it uses (lgemu's)
#   kbuild.sh tree    SRC OBJ CROSS_COMPILE MODULES OUT KO...
#                                                          MODULES (the linux step's) with each KO
#                                                          in <release>/extra/ and depmod run
#                                                          again -> OUT, the tree the initramfs
#                                                          carries
#
# SRC is the pristine unpacked tree (subprojects/<name>.wrap, patches applied), built out of tree in OBJ,
# so make's own dependency tracking makes a re-run incremental. OUT is only replaced when the
# result differs (Meson's custom targets use restat: an unchanged kernel does not repack anything).
#
# The kernel's build stamp (the banner's user@host, #version and date, and the mtime of its
# built-in default initramfs) is fixed, so the same inputs give the same Image in every target's
# build directory and on every machine: build/Image does not depend on which target built last.
set -eu
what=$1 SRC=$2 OBJ=$3
export ARCH=arm64 CROSS_COMPILE="$4"
export KBUILD_BUILD_TIMESTAMP="1970-01-01 00:00:00 UTC" KBUILD_BUILD_USER=beanjar KBUILD_BUILD_HOST=beanjar \
	KBUILD_BUILD_VERSION=1
shift 4
JOBS=$(nproc)
# ccache, when the machine has it, for the kernel and the external modules: every target's build
# directory compiles the same kernel. Kbuild's compile lines name the object tree by relative
# paths and the shared source tree by its absolute one, so one target's objects are hits for the
# next as they are. No CCACHE_BASEDIR: it would make the source paths relative too, and
# -fmacro-prefix-map would no longer strip them from __FILE__ (the Image would differ).
# BEANJAR_CCACHE=0 turns it off; the Image is the same either way.
KCC="${CROSS_COMPILE}gcc"
if [ "${BEANJAR_CCACHE:-1}" != 0 ] && command -v ccache >/dev/null 2>&1; then
	KCC="ccache $KCC"
	unset CCACHE_BASEDIR
	export CCACHE_NOHASHDIR=1
fi
mkdir -p "$OBJ"

update() { # update BUILT OUT
	cmp -s "$1" "$2" || { cp "$1" "$2.tmp" && mv -f "$2.tmp" "$2"; }
}

case $what in
linux)
	# DEFCONFIG (guest/kernel/configs/webos_virt_defconfig) is a savedefconfig file. It goes where Kbuild
	# looks for <name>_defconfig, arch/arm64/configs/ of the object tree (conf reads the objtree
	# before the srctree), and `make <name>_defconfig` expands it into .config. That only happens
	# when the file changed or there is no .config: otherwise make keeps the .config and its own
	# syncconfig follows a changed source tree.
	DEFCONFIG=$1 OUT=$2 CPIO=$3 MODULES=$4
	name=$(basename "$DEFCONFIG")
	mkdir -p "$OBJ/arch/arm64/configs"
	if ! [ -f "$OBJ/.config" ] || ! cmp -s "$DEFCONFIG" "$OBJ/arch/arm64/configs/$name"; then
		cp "$DEFCONFIG" "$OBJ/arch/arm64/configs/$name"
		make -s -C "$SRC" O="$OBJ" CC="$KCC" "$name" || { rm -f "$OBJ/arch/arm64/configs/$name"; exit 1; }
		# an option whose dependency is off is dropped without a word: every "=y" must survive
		missing=$(grep '^CONFIG_.*=y$' "$DEFCONFIG" | while read -r l; do
			grep -qx "$l" "$OBJ/.config" || echo "${l%=y}"; done)
		[ -z "$missing" ] || { rm -f "$OBJ/arch/arm64/configs/$name"
			echo "kbuild.sh: $name: not set after defconfig:" $missing >&2; exit 1; }
	fi
	# Image builds usr/ too (BLK_DEV_INITRD: the kernel's built-in default initramfs), and with it
	# the gen_init_cpio host program. `modules` builds the defconfig's =m drivers (the USB class
	# drivers the initramfs loads) and runs modpost over vmlinux: its Module.symvers lets an
	# external module's symbols be checked against the exports.
	make -s -C "$SRC" O="$OBJ" CC="$KCC" -j"$JOBS" Image modules
	update "$OBJ/arch/arm64/boot/Image" "$OUT"
	update "$OBJ/usr/gen_init_cpio" "$CPIO"
	# modules_install, stripped of debug sections, runs the host's depmod for modules.dep,
	# modules.alias and modules.builtin; the build symlink points into this machine's tree and
	# is left out. MODULES is replaced only when something in it changed.
	rm -rf "$OBJ/modules-install"
	make -s -C "$SRC" O="$OBJ" CC="$KCC" INSTALL_MOD_PATH="$OBJ/modules-install" INSTALL_MOD_STRIP=1 \
		modules_install
	ls "$OBJ"/modules-install/lib/modules/*/modules.dep >/dev/null ||
		{ echo "kbuild.sh: no modules.dep (is depmod, kmod, installed?)" >&2; exit 1; }
	rm -f "$OBJ"/modules-install/lib/modules/*/build "$OBJ"/modules-install/lib/modules/*/source
	if ! diff -r "$OBJ/modules-install/lib/modules" "$MODULES" >/dev/null 2>&1; then
		rm -rf "$MODULES.tmp" && cp -a "$OBJ/modules-install/lib/modules" "$MODULES.tmp"
		rm -rf "$MODULES" && mv "$MODULES.tmp" "$MODULES"
	fi ;;
busybox)
	# CONFIG (guest/initramfs/busybox/configs/webos_initramfs.config) is a config fragment over allnoconfig: every
	# option it does not set is off. busybox's Kconfig has no savedefconfig, its <name>_defconfig
	# files are whole .configs, and its allnoconfig forces every bool to n even when
	# KCONFIG_ALLCONFIG sets it, so the fragment is merged the way the kernel's
	# scripts/kconfig/merge_config.sh -n does it: allnoconfig, the fragment's lines replace
	# those options, and oldconfig settles dependencies. That happens when the fragment changed
	# (its copy in OBJ differs) or there is no .config. Every "=y" of the fragment must survive
	# (a typo or an unmet dependency fails the build). KCONFIG_NOTIMESTAMP keeps the time of the
	# configuration out of the binary.
	CONFIG=$1 OUT=$2
	export KCONFIG_NOTIMESTAMP=1
	name=$(basename "$CONFIG")
	if ! [ -f "$OBJ/.config" ] || ! cmp -s "$CONFIG" "$OBJ/$name"; then
		rm -f "$OBJ/$name"
		make -s -C "$SRC" O="$OBJ" allnoconfig >/dev/null
		awk 'FNR == NR { if (/^CONFIG_/) { k = $0; sub(/=.*/, "", k); v[k] = $0 } next }
		     { k = "" }
		     /^CONFIG_/ { k = $0; sub(/=.*/, "", k) }
		     /^# CONFIG_[A-Za-z0-9_]* is not set$/ { k = $2 }
		     k in v { print v[k]; delete v[k]; next }
		     { print }
		     END { for (k in v) print v[k] }' "$CONFIG" "$OBJ/.config" > "$OBJ/.config.merged"
		mv -f "$OBJ/.config.merged" "$OBJ/.config"
		yes "" | make -s -C "$SRC" O="$OBJ" oldconfig >/dev/null
		missing=$(grep '^CONFIG_.*=y$' "$CONFIG" | while read -r l; do
			grep -qx "$l" "$OBJ/.config" || echo "${l%=y}"; done)
		[ -z "$missing" ] || { echo "kbuild.sh: $name: not set after oldconfig:" $missing >&2; exit 1; }
		cp "$CONFIG" "$OBJ/$name"
	fi
	make -s -C "$SRC" O="$OBJ" -j"$JOBS" busybox
	update "$OBJ/busybox" "$OUT" ;;
module)
	# MSRC stays clean: MO= puts the objects in MOBJ. The kernel target has built OBJ, with its
	# Module.symvers, before this runs, and the modules of SYMVERS theirs.
	MSRC=$1 MOBJ=$2 OUT=$3
	shift 3
	# Kbuild does not see a changed kernel config here: its objects' CONFIG_ dependencies are
	# `include/config/<SYMBOL>` relative to MOBJ, where make runs, not to OBJ. So a config other
	# than the one MOBJ was built with (autoconf.h) rebuilds the module from clean; a struct whose
	# layout follows an option (struct device and CONFIG_PM_SLEEP) would be wrong otherwise.
	cmp -s "$OBJ/include/generated/autoconf.h" "$MOBJ/.autoconf.h" || rm -rf "$MOBJ"
	mkdir -p "$MOBJ"
	make -s -C "$SRC" O="$OBJ" CC="$KCC" M="$MSRC" MO="$MOBJ" KBUILD_EXTRA_SYMBOLS="$*" -j"$JOBS" modules
	cp "$OBJ/include/generated/autoconf.h" "$MOBJ/.autoconf.h"
	update "$MOBJ/$(basename "$OUT")" "$OUT" ;;
tree)
	# The out-of-tree modules that load with the kernel's own (lgfs.ko: modprobe, boot.conf's
	# PERIPHERAL_MODULES), where modules_install puts an external module (extra/), and the host's
	# depmod over the whole tree for its modules.dep and modules.alias. SRC, OBJ and CROSS_COMPILE
	# are unused. OUT is replaced only when something in it changed.
	MODULES=$1 OUT=$2
	shift 2
	release=$(ls "$MODULES")
	[ "$(echo "$release" | wc -l)" = 1 ] || { echo "kbuild.sh: $MODULES: not one release" >&2; exit 1; }
	stage="$OUT.stage"
	rm -rf "$stage" && mkdir -p "$stage/lib/modules"
	cp -a "$MODULES/$release" "$stage/lib/modules/"
	mkdir -p "$stage/lib/modules/$release/extra"
	for ko in "$@"; do cp "$ko" "$stage/lib/modules/$release/extra/"; done
	depmod -b "$stage" "$release"
	if ! diff -r "$stage/lib/modules" "$OUT" >/dev/null 2>&1; then
		rm -rf "$OUT" && mv "$stage/lib/modules" "$OUT"
	fi
	rm -rf "$stage" ;;
*) echo "kbuild.sh: unknown $what" >&2; exit 1 ;;
esac
