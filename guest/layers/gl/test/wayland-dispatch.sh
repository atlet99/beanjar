#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Native regression, no VM or display required. Uses the archive the guest build fetched
# (subprojects/packagecache, subprojects/wayland.wrap); needs a C compiler, libffi/expat headers, Meson and Ninja.
# BASELINE=1 builds the unpatched library and must fail (alarm on first case).
set -eu
TOP=$(cd "$(dirname "$0")/../../../.." && pwd)
VERSION=${WAYLAND_VER:-1.26.0}
BASELINE=${BASELINE:-0}
OUT=$TOP/build/wayland-dispatch-test-$BASELINE
ARCHIVE=${MESON_PACKAGE_CACHE_DIR:-$TOP/subprojects/packagecache}/wayland-$VERSION.tar.xz
MESON=${MESON:-meson}
[ -f "$ARCHIVE" ] || { echo "Run scripts/build.sh guest first" >&2; exit 1; }
mkdir -p "$OUT"
# Keep compiler/patch temporary files in the build tree in restricted workspaces.
TMPDIR=${TMPDIR:-$OUT/tmp}
CCACHE_DIR=${CCACHE_DIR:-$OUT/ccache}
export TMPDIR CCACHE_DIR
mkdir -p "$TMPDIR" "$CCACHE_DIR"
if [ ! -d "$OUT/wayland-$VERSION" ]; then tar -C "$OUT" -xf "$ARCHIVE"; fi
SRC=$OUT/wayland-$VERSION
if [ "$BASELINE" = 0 ]; then
    for p in "$TOP"/layers/gl/patches/wayland/*.patch; do
        patch -d "$SRC" -p1 -R -s -f --dry-run < "$p" >/dev/null 2>&1 ||
            patch -d "$SRC" -p1 -s -N < "$p"
    done
fi
if [ ! -f "$OUT/obj/build.ninja" ]; then
    "$MESON" setup "$OUT/obj" "$SRC" --buildtype=debugoptimized \
        -Ddocumentation=false -Dtests=false -Ddtd_validation=false
fi
ninja -C "$OUT/obj"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -g -pthread \
    -I"$SRC/src" -I"$OUT/obj/src" "$TOP/layers/gl/test/wayland-dispatch.c" \
    -L"$OUT/obj/src" -Wl,-rpath,"$OUT/obj/src" -lwayland-client -o "$OUT/test"
"$OUT/test"
