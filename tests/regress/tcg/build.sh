#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The benchmark programs and the test-only LS2 role layer tcg-perf come from the guest build
# (tests/regress/tcg/meson.build: build/tcg-perf/{perf-arm,io-perf}, build/layers/<chip>-<release>/tcg-perf). --io also makes
# the storage fixture, build/tcg-perf/io.ext4.
set -eu
TOP=$(cd "$(dirname "$0")/../../.." && pwd)
FW=$("$TOP/host/scripts/vm-launcher" config FW)
"$TOP/scripts/build.sh" guest
if [ "${1:-}" = --io ]; then
    mkdir -p "$TOP/build/tcg-perf/io-fixture"
    cp "$FW/rootfs/usr/lib/libQt5WebKit.so.5.0.0" "$TOP/build/tcg-perf/io-fixture/"
    # Create a fresh scratch image; firmware and the VM's persistent state stay untouched.
    rm -f "$TOP/build/tcg-perf/io.ext4"
    truncate -s 64M "$TOP/build/tcg-perf/io.ext4"
    mkfs.ext4 -q -F -d "$TOP/build/tcg-perf/io-fixture" "$TOP/build/tcg-perf/io.ext4"
fi
