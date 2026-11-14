#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the open test guest and what boots it, then boot it through the launcher and run its checks
# (docs/dev/architecture/test-guest.md): the Test guest workflow's job, .github/workflows/testguest.yml.
# Run as root in a throwaway Debian 13 container: it installs packages into the system it runs in.
#
#   scripts/testguest-ci.sh
#
# 1. Packages: Buildroot's host tools, and the build dependencies of roles guest and host
#    (packaging/debian/build-deps.sh), which have what running a VM needs.
# 2. The guest: guest/testguest/build.sh (Buildroot; BR2_DL_DIR and BR2_CCACHE_DIR, when set, are
#    the caches the workflow keeps), then the kernel and initramfs, build/guest-virt-testguest with
#    Buildroot's toolchain as the 32-bit cross compiler (no LG SDK), published to build/.
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
# Buildroot's own requirements (its manual, "System requirements"), and equivs for the roles
apt-get install -y --no-install-recommends git ca-certificates curl wget equivs dpkg-dev python3 \
	build-essential file patch perl bzip2 xz-utils cpio unzip rsync bc
git config --global --get-all safe.directory | grep -qxF "$TOP" || git config --global --add safe.directory "$TOP"
"$TOP/packaging/debian/build-deps.sh" guest host
apt-get install -y --no-install-recommends "$TOP"/build/packages/beanjar-build-deps-guest_*_all.deb \
	"$TOP"/build/packages/beanjar-build-deps-host_*_all.deb
# virgl on the host without a GPU: QEMU's surfaceless EGL display on Mesa's software rasteriser
apt-get install -y --no-install-recommends libgl1-mesa-dri libegl-mesa0
t1=$(date +%s)

step "test guest (Buildroot)"
# Buildroot refuses to run as root unless told; the container is throwaway
export FORCE_UNSAFE_CONFIGURE=1
"$TOP/guest/testguest/build.sh"
t2=$(date +%s)

step "kernel and initramfs"
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
echo "seconds: packages $((t1 - t0)), test guest $((t2 - t1)), kernel $((t3 - t2)), host $((t4 - t3)), check $((t5 - t4)), total $((t5 - t0))"
exit $rc
