#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Build what boots the open test guest, then boot it through the launcher and run its checks
# (docs/dev/architecture/test-guest.md): the Test guest workflow's job, .github/workflows/testguest.yml.
# Run as root in a throwaway Debian 13 container: it installs packages into the system it runs in.
#
#   scripts/testguest-ci.sh
#
# 1. Packages: the build dependencies of roles guest and host (packaging/debian/build-deps.sh),
#    which have what running a VM needs and Debian's cross compilers (armel for the 32-bit checks,
#    arm64 for the kernel, the initramfs and the 64-bit check with its glibc).
# 2. The guest: scripts/testguest-setup takes beanjar-vendor's userland (packaging/vendor.toml, or
#    the tarball BEANJAR_VENDOR_TESTGUEST names) into build/vendor, then build/guest-virt-testguest
#    builds the testguest layer (the 32-bit checks against the userland's sysroot, and the 64-bit
#    one on Debian's AArch64 glibc), the kernel and the initramfs, published to build/.
# 3. The host build, build/host: the prebuilt QEMU (packaging/vendor.toml) and the media helper.
# 4. scripts/testguest-check, in a library of its own (build/testguest/library); its exit status is
#    this script's. The results are also in build/testguest/results.txt, the VM's logs and the
#    guest's output in build/testguest/logs.
set -eu
TOP=$(cd "$(dirname "$0")/.." && pwd)
export DEBIAN_FRONTEND=noninteractive

[ "$(id -u)" = 0 ] || { echo "testguest-ci.sh: run as root in a throwaway Debian 13 container" >&2; exit 1; }
grep -q '^VERSION_ID="13"' /etc/os-release || { echo "testguest-ci.sh: Debian 13 required" >&2; exit 1; }

step() { echo "== $* ($(date -u +%T))"; }
t0=$(date +%s)

step "packages"
apt-get update
apt-get install -y --no-install-recommends git ca-certificates equivs dpkg-dev python3 xz-utils
git config --global --get-all safe.directory | grep -qxF "$TOP" || git config --global --add safe.directory "$TOP"
"$TOP/packaging/debian/build-deps.sh" guest host
apt-get install -y --no-install-recommends "$TOP"/build/packages/beanjar-build-deps-guest_*_all.deb \
	"$TOP"/build/packages/beanjar-build-deps-host_*_all.deb
# virgl on the host without a GPU: QEMU's surfaceless EGL display on Mesa's software rasteriser
apt-get install -y --no-install-recommends libgl1-mesa-dri libegl-mesa0
t1=$(date +%s)

step "test guest userland"
"$TOP/scripts/testguest-setup"
t2=$(date +%s)

step "testguest layer, kernel and initramfs"
dir=$TOP/build/guest-virt-testguest
[ -f "$dir/build.ninja" ] || meson setup --cross-file "$TOP/build/testguest/cross.ini" "$dir" "$TOP" \
	-Dsoc=virt -Dwebos=testguest
meson compile -C "$dir"
t3=$(date +%s)

step "host build"
[ -f "$TOP/build/host/build.ninja" ] || meson setup "$TOP/build/host" "$TOP" -Dfirmware_cache=disabled \
	-Dvirtcam=disabled
meson compile -C "$TOP/build/host"
t4=$(date +%s)

step "check"
rc=0
"$TOP/scripts/testguest-check" --library "$TOP/build/testguest/library" --logs "$TOP/build/testguest/logs" \
	> "$TOP/build/testguest/results.txt" || rc=$?
cat "$TOP/build/testguest/results.txt"
t5=$(date +%s)
echo "seconds: packages $((t1 - t0)), userland $((t2 - t1)), guest build $((t3 - t2)), host $((t4 - t3)), check $((t5 - t4)), total $((t5 - t0))"
exit $rc
