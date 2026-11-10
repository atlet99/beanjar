#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Build neatvnc 1.0 and aml 1.0 for Debian trixie, amd64 and arm64, as local packages with
# Debian's names and sonames (libneatvnc1, libneatvnc-dev, libaml1, libaml-dev). The packaging is
# Debian's own (forky's source packages, fetched from the signed archive); neatvnc's upstream is
# the newer 1.0.4, with the V4L2 H.264 fixes in patches/ as debian/patches. Built on a Debian
# trixie amd64 host with sudo and mmdebstrap: the build runs in a throwaway chroot under $WORK
# (removed by mmdebstrap when it exits); only the .debs and the build log are kept, in $WORK/out.
# See docs/dev/guides/neatvnc-local.md.
#
# Started by a normal user, the script re-runs itself as the transient root unit
# neatvnc-local-build, so the whole build is one cgroup: follow it with
# `tail -f $WORK/out/build.log`, stop it with `sudo systemctl stop neatvnc-local-build`. Never
# stop it with pkill -f / pgrep -f patterns: they match the invoking shell too.
#
# ARCHS (default "amd64 arm64") are the architectures built for; amd64 builds natively and runs
# neatvnc's tests, the others are cross-built without them.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
WORK=${WORK:-$repo/scratch/neatvnc-local}
UNIT=neatvnc-local-build
ARCHS=${ARCHS:-amd64 arm64}

if [ "$(id -u)" != 0 ]; then
	mkdir -p "$WORK"
	sudo systemd-run --unit="$UNIT" --collect --property=WorkingDirectory="$WORK" \
		--setenv=HOME=/root --setenv=WORK="$WORK" --setenv=ARCHS="$ARCHS" \
		--setenv=OWNER="$(id -u):$(id -g)" /bin/sh "$here/build.sh"
	echo "log: $WORK/out/build.log; stop: sudo systemctl stop $UNIT"
	exit 0
fi
OWNER=${OWNER:-0:0}

# Debian (forky) source packages, and the local versions built from them. ~bpo13 sorts below
# the Debian version it comes from, so an official backport or a trixie update supersedes it.
AML_DEB=1.0.0-1
AML_LOCAL=1.0.0-1~bpo13+beanjar1
# neatvnc: Debian's 1.0.1+dfsg-1 packaging on upstream 1.0.4 (-0: no Debian revision of 1.0.4).
NVNC_DEB=1.0.1+dfsg-1
NVNC_UPSTREAM=1.0.4
NVNC_LOCAL=1.0.4+dfsg-0~bpo13+beanjar2
NVNC_URL=https://github.com/any1/neatvnc/archive/refs/tags/v$NVNC_UPSTREAM.tar.gz
NVNC_SHA256=c40d74bb788a60519e6c926f04515502de7859abcfd74f2a18f97bd02f4dc231
# debian/copyright's Files-Excluded, for the +dfsg repack
NVNC_EXCLUDED="doc test-images include/xxhash.h"

[ "$(dpkg --print-architecture)" = amd64 ] || { echo "build on an amd64 host" >&2; exit 1; }
archs=amd64
cross=
for a in $ARCHS; do
	[ "$a" = amd64 ] && continue
	archs=$archs,$a
	# libc6-dev too: aml's build-deps pull in no library of the host architecture
	cross="$cross${cross:+,}crossbuild-essential-$a,libc6-dev:$a"
done

rm -rf "$WORK/out"
mkdir -p "$WORK/tmp" "$WORK/out"
log=$WORK/out/build.log
exec > "$log" 2>&1
stage=$WORK/stage
rm -rf "$stage"
mkdir -p "$stage/patches"
cp "$here"/patches/*.patch "$stage/patches/"
(cd "$stage/patches" && ls *.patch) > "$stage/patches/series"
curl -fsSL -o "$stage/neatvnc-$NVNC_UPSTREAM.tar.gz" "$NVNC_URL"
echo "$NVNC_SHA256  $stage/neatvnc-$NVNC_UPSTREAM.tar.gz" | sha256sum -c

cat > "$stage/inside.sh" <<EOF
#!/bin/sh
set -eux
export DEBIAN_FRONTEND=noninteractive
apt-get update

# changelog <version> <lines...>: a local entry on top of debian/changelog
changelog() {
	version=\$1
	shift
	{
		echo "\$(dpkg-parsechangelog -S Source) (\$version) trixie; urgency=medium"
		echo
		for line; do echo "\$line"; done
		echo
		echo " -- Beanjar local build <beanjar@localhost>  \$(date -R)"
		echo
		cat debian/changelog
	} > debian/changelog.new
	mv debian/changelog.new debian/changelog
}

# build <arch>: binary packages of the source tree in . for <arch>, kept in /build/out
build() {
	if [ "\$1" = amd64 ]; then
		apt-get build-dep -y .
		DEB_BUILD_OPTIONS="parallel=\$(nproc)" dpkg-buildpackage -B -us -uc
	else
		apt-get build-dep -y -a "\$1" -P nocheck .
		DEB_BUILD_OPTIONS="nocheck parallel=\$(nproc)" DEB_BUILD_PROFILES=nocheck \
			dpkg-buildpackage -B -us -uc -a "\$1"
	fi
	mkdir -p /build/out
	cp ../*.deb ../*.buildinfo ../*.changes /build/out/
	rm -f ../*.deb ../*.buildinfo ../*.changes
}

start=\$(date +%s)
echo "build start \$(date +%T)"
mkdir -p /build/src
cd /build/src

# aml: Debian's source as it is
apt-get source --only-source aml=$AML_DEB
cd aml-1.0.0
changelog $AML_LOCAL "  * Local build of Debian's $AML_DEB for trixie (for neatvnc 1.0)."
for a in $ARCHS; do build \$a; done
cd /build/src
apt-get install -y \$(for a in $ARCHS; do echo /build/out/libaml1_*_\$a.deb /build/out/libaml-dev_*_\$a.deb; done)

# neatvnc: Debian's packaging on upstream $NVNC_UPSTREAM, repacked without Files-Excluded
apt-get source --download-only --only-source neatvnc=$NVNC_DEB
tar -xzf /build/neatvnc-$NVNC_UPSTREAM.tar.gz
cd neatvnc-$NVNC_UPSTREAM
rm -rf $NVNC_EXCLUDED
cd ..
tar -cJf neatvnc_$NVNC_UPSTREAM+dfsg.orig.tar.xz neatvnc-$NVNC_UPSTREAM
cd neatvnc-$NVNC_UPSTREAM
tar -xJf ../neatvnc_$NVNC_DEB.debian.tar.xz
# dh_makeshlibs would ask for this build's version (>= 1.0.4+dfsg), which Raspberry Pi OS's
# 1.0.4-1+rpt1 does not satisfy; 1.0.1 to 1.0.4 add no API, so 1.0.0 is enough
printf '\noverride_dh_makeshlibs:\n\tdh_makeshlibs -V"libneatvnc1 (>= 1.0.0)"\n' >> debian/rules
mkdir -p debian/patches
cp /build/patches/*.patch /build/patches/series debian/patches/
changelog $NVNC_LOCAL \
	"  * Local build of upstream $NVNC_UPSTREAM with Debian's $NVNC_DEB packaging," \
	"    for trixie (FFmpeg 7.1)." \
	"  * V4L2 mem2mem H.264 fixes as debian/patches: the DRM fourcc tried" \
	"    as the V4L2 format first, and the constant quality control mapped" \
	"    to its 1..100 range."
for a in $ARCHS; do build \$a; done
echo "build seconds: \$((\$(date +%s) - start))"
EOF

status=0
TMPDIR="$WORK/tmp" mmdebstrap --variant=buildd --mode=root --architectures="$archs" \
	--aptopt='APT::Install-Recommends "false"' \
	--include="ca-certificates${cross:+,$cross}" \
	--customize-hook='mkdir -p "$1/build"' \
	--customize-hook="copy-in $stage/inside.sh $stage/patches $stage/neatvnc-$NVNC_UPSTREAM.tar.gz /build" \
	--customize-hook='chroot "$1" sh /build/inside.sh' \
	--customize-hook="sync-out /build/out $WORK/out" \
	trixie /dev/null \
	"deb http://deb.debian.org/debian trixie main" \
	"deb http://deb.debian.org/debian trixie-updates main" \
	"deb http://deb.debian.org/debian-security trixie-security main" \
	"deb-src http://deb.debian.org/debian forky main" || status=$?
chown -R "$OWNER" "$WORK/out"
rm -rf "$stage" "$WORK/tmp"
[ "$status" = 0 ] && grep -q '^build seconds:' "$log" || { echo "build failed ($status)"; exit 1; }
ls -l "$WORK/out"
