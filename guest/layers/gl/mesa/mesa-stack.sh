#!/bin/bash
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Cross-build the gl layer's graphics stack for a webOS 1, 2 or 3 userland (ARMv7 softfp, glibc <= the
# target's ceiling, loader /lib/ld-linux.so.3) with the webOS SDK: libffi 3.0.13, libdrm, Wayland
# 1.26 (+ wayland-protocols) and Mesa (gallium virgl + softpipe; EGL surfaceless/drm/wayland,
# GLES2, gbm). guest/layers/gl/mesa/meson.build runs it; docs/dev/guides/build.md says why these are separate
# builds rather than Meson subprojects.
#
#   mesa-stack.sh deps WORK STAGE STAMP DEPFILE
#       libffi, libdrm, Wayland, wayland-protocols
#   mesa-stack.sh mesa WORK STAGE STAMP DEPFILE [COMPAT]
#       Mesa, after guest/layers/gl/mesa/meson.build has linked libwayland-server-compat (COMPAT)
#       against the Wayland server library of the first step (webOS 1 and 2; without COMPAT, for
#       webOS 3's Wayland 1.7, Mesa links the server library alone)
#
#   WORK     build directories of the five packages (each incremental) and a native wayland-scanner;
#            shared by the targets whose builds are the same (build/mesa/<key>/work), locked while used
#   STAGE    the result (build/mesa/<key>/stage), a /usr prefix: usr/lib/{libEGL.so.1.0.0,libGLESv2.so.2.0.0,
#            libgallium-*.so,libgbm.so.1.0.0,gbm/dri_gbm.so,libdrm.so.2.*,libwayland-egl.so.1.*,
#            libwayland-server-compat.so.0,webos/libwayland-client.so.0}, headers, pkg-config files
#   STAMP    the target's stamp: rewritten only when STAGE's contents change
#   DEPFILE  names WORK/<step>.manifest, the shared copy of the stamp: a target notices a change
#            another target made, and a removed build/mesa, by it
# Env (all set by guest/layers/gl/mesa/meson.build):
#   MESON    the Meson to use; CROSS the inner builds' cross file (compilers, flags, pkg-config),
#            copied to WORK/cross.ini, beside the pkg-config wrapper it names (WORK/pkg-config)
#   UAPI     kernel uapi headers newer than the SDK's 3.10 ones (Debian's armel cross headers)
#   GUEST_CC, ARM_FLAGS  the guest compiler and its ISA flags, for libffi and the relink
#   FFI_SRC, DRM_SRC, WL_SRC, WLP_SRC, MESA_SRC  the unpacked, patched trees (subprojects/)
#   FW_WL_SRC       the firmware's own Wayland version's tree (firmware_wayland), or empty: Mesa is
#                   built against it instead of WL_SRC's 1.26, with its scanner (Mesa wants the
#                   client's version), and no libwayland-client is relinked for the layer
#   MESA_PLATFORMS  Mesa's EGL window systems (default wayland; NetCast 4.5: none, surfaceless only)
#   DRM_EXTRA_ARGS  more libdrm options (NetCast 4.5: static, inside Mesa's libraries)
#   MESA_EXTRA_ARGS more Mesa options (NetCast 4.5: its libEGL renamed, under the window system's)
#
# Each package is installed into its own DESTDIR and synced into STAGE by content, so a re-run
# that changes nothing leaves every mtime (and so Mesa's link steps) alone.
set -euo pipefail
STEP=$1 WORK=$2 STAGE=$3 STAMP=$4 DEPFILE=$5 COMPAT=${6:-}
HERE=$(dirname "$(readlink -f "$0")")
JOBS=$(nproc)
# Mesa's configure takes the first python3.N it finds on PATH, so PATH is only the directory of the
# Meson that runs it and the system directories (python3 with mako, yaml and packaging: Debian's
# python3-mako, python3-yaml, python3-packaging): no pyenv shims or the like.
PATH=$(dirname "$(command -v "$MESON")"):/usr/local/bin:/usr/bin:/bin
export PATH
mkdir -p "$WORK" "$STAGE"
WORK=$(readlink -f "$WORK") STAGE=$(readlink -f "$STAGE")
# WORK and STAGE are shared by every target with the same configuration (build/mesa/<key>,
# guest/layers/gl/mesa/meson.build): one run at a time, the others wait and then find it done.
exec 9>"$WORK/.lock"
flock 9

# The SDK's kernel uapi (3.10) is too old for Mesa's drm/virgl code: newer armel headers go first
# (CROSS names $WORK/uapi).
mkdir -p "$WORK/uapi"
for d in linux asm asm-generic drm; do
	[ -d "$WORK/uapi/$d" ] || cp -a "$UAPI/$d" "$WORK/uapi/"
done
# The cross file and its pkg-config, at the shared directory's own paths (the target's copy of the
# cross file is the same for every target sharing it; the setup keys below name the path)
cmp -s "$CROSS" "$WORK/cross.ini" || cp "$CROSS" "$WORK/cross.ini"
PKGCONF="#!/bin/sh
# pkg-config for the Mesa stack's inner builds: only the stage's .pc files, never the SDK's own
# wayland 1.11 / libffi.so.8 / EGL (written by guest/layers/gl/mesa/mesa-stack.sh).
export PKG_CONFIG_DIR=
export PKG_CONFIG_LIBDIR=$STAGE/usr/lib/pkgconfig:$STAGE/usr/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=
exec pkg-config \"\$@\""
[ "$(cat "$WORK/pkg-config" 2>/dev/null)" = "$PKGCONF" ] || { printf '%s\n' "$PKGCONF" > "$WORK/pkg-config"; chmod 755 "$WORK/pkg-config"; }
CROSSARGS="--cross-file $WORK/cross.ini"
CROSS_KEY=$(sha256sum "$WORK/cross.ini" | cut -d' ' -f1)

# finish: the shared manifest of STAGE (rewritten when it changes), the target's STAMP from it, and
# DEPFILE. A STAMP equal to the manifest but older (the stage changed and changed back since this
# target last ran) is touched, as the depfile's rule has no restat.
finish() {
	python3 "$HERE/../../../../meson/stage.py" --manifest "$STAGE" "$WORK/$STEP.manifest"
	if ! cmp -s "$WORK/$STEP.manifest" "$STAMP"; then
		cp "$WORK/$STEP.manifest" "$STAMP"
	elif [ "$STAMP" -ot "$WORK/$STEP.manifest" ]; then
		touch "$STAMP"
	fi
	printf '%s: %s\n' "$STAMP" "$WORK/$STEP.manifest" > "$DEPFILE"
}

# sync INSTALLDIR: copy into STAGE what differs (by content), rewriting .pc prefixes to STAGE
sync() {
	find "$1" -name '*.pc' -exec sed -i "s|^prefix=/usr\$|prefix=$STAGE/usr|" {} +
	rsync -rlpc "$1/" "$STAGE/"
}

# setup NAME SRC ARGS...: (re)configure a Meson build when its arguments (or SETUP_KEY) changed
setup() {
	local b=$WORK/$1 s=$2; shift 2
	local key="$s $* $CROSS_KEY ${SETUP_KEY-}"
	if [ ! -f "$b/build.ninja" ] || [ "$(cat "$b/.setup-args" 2>/dev/null)" != "$key" ]; then
		rm -rf "$b"
		"$MESON" setup "$b" "$s" "$@" >"$WORK/$(basename "$b").setup.log" 2>&1 ||
			{ tail -n 30 "$WORK/$(basename "$b").setup.log" >&2; exit 1; }
		printf '%s\n' "$key" > "$b/.setup-args"
	fi
}
# build NAME: ninja, then install into WORK/inst-NAME and sync it into STAGE
build() {
	ninja -C "$WORK/$1" -j"$JOBS" >"$WORK/$1.build.log" 2>&1 || { tail -n 30 "$WORK/$1.build.log" >&2; exit 1; }
	rm -rf "$WORK/inst-$1"
	DESTDIR=$WORK/inst-$1 "$MESON" install -C "$WORK/$1" --quiet --no-rebuild
	sync "$WORK/inst-$1"
}

WL=$WL_SRC
NATIVE_PC=$WORK/native/usr/lib/pkgconfig:$WORK/native/usr/share/pkgconfig
if [ "$STEP" = deps ]; then
# ---- libffi 3.0.13 (SONAME libffi.so.6) --------------------------------------------------------
# The SDK ships libffi.so.8. Build pinned libffi 3.0.13 for Wayland and ship its
# libffi.so.6 ABI in the GL layer; newer firmware generations only carry libffi.so.7.
FFI=$FFI_SRC
FFI_KEY="$FFI $GUEST_CC $ARM_FLAGS"
if [ ! -f "$WORK/ffi/Makefile" ] || [ "$(cat "$WORK/ffi/.configured" 2>/dev/null)" != "$FFI_KEY" ]; then
	rm -rf "$WORK/ffi"; mkdir -p "$WORK/ffi"
	(cd "$WORK/ffi" && "$FFI/configure" --host=arm-linux-gnueabi --build="$(uname -m)-linux-gnu" \
		--prefix=/usr --libdir=/usr/lib --disable-static CC="$GUEST_CC" CFLAGS="$ARM_FLAGS -O2" \
		>"$WORK/ffi.configure.log" 2>&1) || { tail -n 30 "$WORK/ffi.configure.log" >&2; exit 1; }
	printf '%s\n' "$FFI_KEY" > "$WORK/ffi/.configured"
fi
make -C "$WORK/ffi" -j"$JOBS" >"$WORK/ffi.build.log" 2>&1 || { tail -n 30 "$WORK/ffi.build.log" >&2; exit 1; }
rm -rf "$WORK/inst-ffi"
make -C "$WORK/ffi" install DESTDIR="$WORK/inst-ffi" >/dev/null
# 3.0.x installs its headers under lib/libffi-3.0.13/include
sed -i "s|^libdir=.*|libdir=$STAGE/usr/lib|; s|^includedir=.*|includedir=$STAGE/usr/lib/${FFI##*/}/include|" \
	"$WORK/inst-ffi/usr/lib/pkgconfig/libffi.pc"
rm -rf "$WORK/inst-ffi/usr/share"   # info pages
sync "$WORK/inst-ffi"

# ---- libdrm ---------------------------------------------------------------------------------------
setup drm "$DRM_SRC" $CROSSARGS --prefix=/usr --libdir=lib --buildtype=release \
	-Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled -Dvmwgfx=disabled \
	-Domap=disabled -Dexynos=disabled -Dfreedreno=disabled -Dtegra=disabled -Dvc4=disabled \
	-Detnaviv=disabled -Dcairo-tests=disabled -Dman-pages=disabled -Dvalgrind=disabled \
	-Dtests=false -Dinstall-test-programs=false -Dudev=false ${DRM_EXTRA_ARGS-}
build drm

# ---- Wayland 1.26 ---------------------------------------------------------------------------------
# Mesa's wayland platform needs libwayland-client/-server >= 1.18 in-process, wayland-egl-backend
# >= 3 and wayland-protocols >= 1.41; the firmware has 1.0.3 (webOS 1) or 1.1 (webOS 2). A cross build of Wayland needs a
# wayland-scanner of exactly its own version on the build machine: built natively here.
setup wl-native "$WL" --prefix="$WORK/native/usr" --libdir=lib -Dlibraries=false \
	-Ddocumentation=false -Dtests=false -Ddtd_validation=false
ninja -C "$WORK/wl-native" install >"$WORK/wl-native.build.log" 2>&1 || { tail -n 30 "$WORK/wl-native.build.log" >&2; exit 1; }
# With FW_WL_SRC, the firmware's version instead, for Mesa to compile and link against only (never
# shipped), cross-built with a scanner of its own version. A change of version first removes the
# other's libraries and headers from STAGE (sync only adds and replaces).
WL_LIB=${FW_WL_SRC:-$WL}
if [ "$(cat "$STAGE/.wayland" 2>/dev/null)" != "$WL_LIB" ]; then
	rm -rf "$STAGE"/usr/lib/libwayland-* "$STAGE"/usr/lib/webos "$STAGE"/usr/include/wayland-*.h \
		"$STAGE"/usr/lib/pkgconfig/wayland-{client,server,egl,egl-backend,cursor}.pc
	printf '%s\n' "$WL_LIB" > "$STAGE/.wayland"
fi
WL_PC=$NATIVE_PC
if [ -n "${FW_WL_SRC-}" ]; then
	setup wl-fw-native "$FW_WL_SRC" --prefix="$WORK/native-fw/usr" --libdir=lib -Dlibraries=false \
		-Ddocumentation=false -Dtests=false -Ddtd_validation=false
	ninja -C "$WORK/wl-fw-native" install >"$WORK/wl-fw-native.build.log" 2>&1 ||
		{ tail -n 30 "$WORK/wl-fw-native.build.log" >&2; exit 1; }
	WL_PC=$WORK/native-fw/usr/lib/pkgconfig:$WORK/native-fw/usr/share/pkgconfig
fi
setup wl "$WL_LIB" $CROSSARGS --prefix=/usr --libdir=lib --buildtype=release -Dscanner=false \
	-Ddocumentation=false -Dtests=false -Ddtd_validation=false -Dbuild.pkg_config_path="$WL_PC"
build wl
# Its enum headers are made with the native scanner above: the build machine's may be older than
# the protocols' XML (Debian 13's 1.23 rejects 1.49's "frozen" attribute).
setup wlp "$WLP_SRC" --prefix=/usr -Dtests=false -Dpkg_config_path="$NATIVE_PC"
build wlp
finish
exit 0
fi

# Server side: the compositor keeps the firmware's libwayland-server, 1.0.3, 1.1 or 1.7 (its
# qtwayland, 5.0 or Qt5Compositor 5.1, needs it). On 1.0.3 and 1.1, libwayland-server-compat
# (src/wayland-server-compat.c) adds the 7 newer functions Mesa's EGL_WL_bind_wayland_display uses;
# it goes ahead of libwayland-server on Mesa's link line so those symbols bind to it (NEEDED
# libwayland-server-compat.so.0 in libEGL and dri_gbm). 1.7 has them.
if [ -n "$COMPAT" ]; then
	cmp -s "$COMPAT" "$STAGE/usr/lib/libwayland-server-compat.so.0" ||
		cp "$COMPAT" "$STAGE/usr/lib/libwayland-server-compat.so.0"
	ln -sfn libwayland-server-compat.so.0 "$STAGE/usr/lib/libwayland-server-compat.so"
	grep -q wayland-server-compat "$STAGE/usr/lib/pkgconfig/wayland-server.pc" ||
		sed -i 's|^Libs: -L${libdir} -lwayland-server|Libs: -L${libdir} -lwayland-server-compat -lwayland-server|' \
			"$STAGE/usr/lib/pkgconfig/wayland-server.pc"
fi

# Client side: webOS patched the core protocol inside its Wayland, differently per release (1.0.3
# on webOS 1: wl_shell_surface has extra events 3 state_changed(u) and 4 close(); 1.1 on webOS 2:
# wl_shell_surface is upstream's, but wl_pointer.set_cursor takes fixed hotspots, "u?off"), and
# the firmware's clients were built against that. So the libwayland-client 1.26 the layer ships
# (as libwayland-client.so.0 for every process, the compositor included) does not export its own
# copies of the core interfaces the firmware's Wayland also has: they resolve to the firmware's
# libwayland-server.so.0, which carries the same patched tables as its client library and is made
# a NEEDED library for that purpose, whichever release it is. wl_display/wl_registry/wl_callback are
# identical in both and stay exported, so every user (e.g. Mesa's native display detection) sees
# one wl_display_interface. The compositor needs this too: its qtwayland advertises
# interface->version to clients and implements only its own release's requests, so it must never
# see 1.26's tables.
if [ -z "${FW_WL_SRC-}" ]; then
{ echo "{ local:"
  for i in compositor shm_pool shm buffer data_offer data_source data_device data_device_manager \
           shell shell_surface surface seat pointer keyboard touch output region; do
    echo "  wl_${i}_interface;"; done
  echo "};"; } > "$WORK/webos.ver"
mkdir -p "$WORK/webos"
(cd "$WORK/wl" && $GUEST_CC $ARM_FLAGS -o "$WORK/webos/libwayland-client.so.0" \
	src/libwayland-client.so.0.26.0.p/*.o -shared -fPIC -Wl,-soname,libwayland-client.so.0 \
	-Wl,--version-script,"$WORK/webos.ver" -Wl,-O1 -Wl,--hash-style=both -static-libgcc \
	-Wl,--start-group src/libwayland-private.a src/libwayland-util.a -L"$STAGE/usr/lib" -lffi -lm -pthread -lrt -Wl,--end-group \
	-Wl,--no-as-needed -lwayland-server)
mkdir -p "$STAGE/usr/lib/webos"
cmp -s "$WORK/webos/libwayland-client.so.0" "$STAGE/usr/lib/webos/libwayland-client.so.0" ||
	cp "$WORK/webos/libwayland-client.so.0" "$STAGE/usr/lib/webos/libwayland-client.so.0"
fi

# ---- Mesa -----------------------------------------------------------------------------------------
# Its configure tests the Wayland it builds against (HAVE_WL_*): a change of version reconfigures it.
# No on-disk shader cache, as on a set: the firmware's own GL drivers (PowerVR's libGLESv2_PVR_MESA.so
# and libsrv_um, Mali's libmali) keep none, so an app compiles its shaders at every launch unless it
# saves them itself with GL_OES_get_program_binary. shader-cache-default=false rather than
# shader-cache=disabled: the latter drops the cache object that program binaries are keyed by, and
# with it GL_NUM_PROGRAM_BINARY_FORMATS; this way the object stays and never touches the disk
# (MESA_SHADER_CACHE_DISABLE=false still opts a process in).
SETUP_KEY=$(cat "$STAGE/.wayland") setup mesa "$MESA_SRC" $CROSSARGS --prefix=/usr --libdir=lib --buildtype=release -Db_ndebug=true \
	-Dgallium-drivers=virgl,softpipe -Dvulkan-drivers= -Dplatforms="${MESA_PLATFORMS-wayland}" \
	-Degl=enabled -Dgles1=enabled -Dgles2=enabled -Dopengl=true -Dglx=disabled -Dglvnd=disabled \
	-Dgbm=enabled -Dllvm=disabled -Dxmlconfig=disabled -Dexpat=disabled \
	-Dzstd=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled \
	-Dbuild-tests=false -Dgallium-va=disabled \
	-Dgallium-extra-hud=false -Dtools= -Dmicrosoft-clc=disabled -Dgallium-rusticl=false \
	-Dandroid-libbacktrace=disabled -Dlegacy-wayland=bind-wayland-display \
	-Dshader-cache=enabled -Dshader-cache-default=false \
	-Dbuild.pkg_config_path="${FW_WL_SRC:+$WORK/native-fw/usr/lib/pkgconfig:$WORK/native-fw/usr/share/pkgconfig:}$NATIVE_PC" \
	${MESA_EXTRA_ARGS-}
build mesa

finish
