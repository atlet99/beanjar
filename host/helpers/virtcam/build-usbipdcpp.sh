#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usbipdcpp's shared libraries for the virtual camera (host/helpers/virtcam/meson.build runs it):
#
#   build-usbipdcpp.sh SRC OBJ STAGE STAMP [CROSS_PREFIX PKG_CONFIG]
#
# SRC is the unpacked tree (subprojects/usbipdcpp.wrap), OBJ its CMake build directory
# (incremental), STAGE receives usr/include/usbipdcpp and usr/lib/libusbipdcpp{,_virtual_device}.so,
# and the directory of STAMP a copy of the two libraries (where the program links them, and finds
# them at run time: RUNPATH $ORIGIN; usbipdcpp's CMake gives its own libraries $ORIGIN too). STAMP
# is rewritten only when STAGE's contents change. A cross build (the arm64 host lane) adds the
# compiler prefix (aarch64-linux-gnu-) and the target's pkg-config.
#
# Only the core and the virtual device component are built: no libusb component, examples, tests
# or Python bindings. Debian 13 gives spdlog and fmt (shared, runtime) and asio (headers; it has
# no CMake config, so pkg-config finds it). The libraries are LGPL-3.0 and stay separate shared
# objects (packaging/debian/build.py installs them privately beside the program).
set -eu
SRC=$1 OBJ=$2 STAGE=$3 STAMP=$4 CROSS_PREFIX=${5:-} CROSS_PKG_CONFIG=${6:-}
set -- -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib \
	-DUSBIPDCPP_BUILD_SHARED_LIBS=ON -DUSBIPDCPP_USE_PKGCONF_ASIO=ON \
	-DUSBIPDCPP_BUILD_VIRTUAL_DEVICE=ON -DUSBIPDCPP_BUILD_LIBUSB_COMPONENTS=OFF \
	-DUSBIPDCPP_BUILD_EXAMPLES=OFF -DUSBIPDCPP_BUILD_EXAMPLE_LIBUSB_SERVER=OFF \
	-DUSBIPDCPP_BUILD_EXAMPLE_LIBEVDEV_MOUSE=OFF -DUSBIPDCPP_BUILD_TESTS=OFF \
	-DUSBIPDCPP_BUILD_PYTHON_BINDINGS=OFF
if [ -n "$CROSS_PREFIX" ]; then
	triplet=${CROSS_PREFIX%-}
	set -- "$@" -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR="${triplet%%-*}" \
		-DCMAKE_C_COMPILER="${CROSS_PREFIX}gcc" -DCMAKE_CXX_COMPILER="${CROSS_PREFIX}g++" \
		-DCMAKE_LIBRARY_ARCHITECTURE="$triplet" -DPKG_CONFIG_EXECUTABLE="$CROSS_PKG_CONFIG"
	export PKG_CONFIG="$CROSS_PKG_CONFIG"
fi
mkdir -p "$OBJ"
OBJ=$(cd "$OBJ" && pwd)
key="$SRC $*"
if [ ! -f "$OBJ/build.ninja" ] || [ "$(cat "$OBJ/.configured" 2>/dev/null)" != "$key" ]; then
	rm -rf "$OBJ"; mkdir -p "$OBJ"
	cmake -S "$SRC" -B "$OBJ" "$@" >"$OBJ/configure.log" 2>&1 ||
		{ tail -n 30 "$OBJ/configure.log" >&2; exit 1; }
	echo "$key" > "$OBJ/.configured"
fi
cmake --build "$OBJ" >"$OBJ/build.log" 2>&1 || { tail -n 30 "$OBJ/build.log" >&2; exit 1; }
rm -rf "$STAGE.new"
DESTDIR="$STAGE.new" cmake --install "$OBJ" >"$OBJ/install.log" 2>&1 ||
	{ tail -n 30 "$OBJ/install.log" >&2; exit 1; }
# only what the program links: headers and the two libraries (no CMake package files)
rm -rf "$STAGE.new/usr/lib/cmake" "$STAGE.new/usr/lib/pkgconfig" "$STAGE.new/usr/share"
# STAGE (and its files' times, which the program's link depends on) changes only with its contents
sum=$(cd "$STAGE.new" && find . -type f | LC_ALL=C sort | xargs sha256sum | sha256sum)
if [ -d "$STAGE" ] && [ "$(cat "$STAMP" 2>/dev/null)" = "$sum" ]; then
	rm -rf "$STAGE.new"
else
	rm -rf "$STAGE"; mv "$STAGE.new" "$STAGE"
	echo "$sum" > "$STAMP"
fi
out=$(dirname "$STAMP")
for lib in libusbipdcpp.so libusbipdcpp_virtual_device.so; do
	cmp -s "$STAGE/usr/lib/$lib" "$out/$lib" || cp "$STAGE/usr/lib/$lib" "$out/$lib"
done
