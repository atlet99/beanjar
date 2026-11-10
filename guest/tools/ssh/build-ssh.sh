#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# One of the guest's SSH programs, configured and built out of tree from a pristine source tree
# (guest/tools/ssh/meson.build runs this on the unpacked subprojects/<name>.wrap trees):
#
#   build-ssh.sh dropbear SRC OBJ OUTDIR LOCALOPTIONS... -- PROGRAM...
#       Dropbear with the concatenation of the LOCALOPTIONS headers as its localoptions.h
#   build-ssh.sh openssh SRC OBJ OUTDIR -- sftp-server
#
# A PROGRAM may be given as NAME=OUTNAME to store it in OUTDIR under another name.
# Env: HOST (configure --host), CC, CFLAGS, STRIP (all from the guest's cross file).
# The stripped programs land in OUTDIR, each replaced only when it changed. configure runs again
# when the options header or the flags change (a stamp in OBJ records them); make is incremental.
set -eu
what=$1 SRC=$2 OBJ=$3 OUTDIR=$4
shift 4
opts=
while [ $# -gt 0 ] && [ "$1" != -- ]; do opts="$opts $1"; shift; done
[ "${1:-}" = -- ] && shift
mkdir -p "$OBJ" "$OUTDIR"

case $what in
dropbear)
	# shellcheck disable=SC2086
	cat $opts > "$OBJ/localoptions.h.new"
	cmp -s "$OBJ/localoptions.h.new" "$OBJ/localoptions.h" || mv -f "$OBJ/localoptions.h.new" "$OBJ/localoptions.h"
	rm -f "$OBJ/localoptions.h.new"
	# No zlib (the forward is local), no login records (the guest keeps none), bundled libtom.
	conf="--disable-zlib --enable-bundled-libtom --disable-lastlog --disable-utmp --disable-utmpx
		--disable-wtmp --disable-wtmpx --disable-pututline --disable-pututxline" ;;
openssh)
	# sftp-server needs neither OpenSSL nor zlib.
	conf="--without-openssl --without-zlib --without-pam --without-selinux --disable-lastlog
		--disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx" ;;
*) echo "build-ssh.sh: unknown $what" >&2; exit 1 ;;
esac

# Configure checks add libcrypt even for key-only servers. Keep only libraries
# the final program actually uses; newer firmware no longer ships libcrypt.
link_flags="${LDFLAGS:-} -Wl,--as-needed"
key="$SRC $HOST $CC $CFLAGS $link_flags $conf"
if [ ! -f "$OBJ/Makefile" ] || [ "$(cat "$OBJ/.configured" 2>/dev/null)" != "$key" ]; then
	[ ! -f "$OBJ/Makefile" ] || make -C "$OBJ" clean >/dev/null 2>&1
	# shellcheck disable=SC2086
	(cd "$OBJ" && "$SRC/configure" --host="$HOST" CC="$CC" CFLAGS="$CFLAGS" LDFLAGS="$link_flags" $conf \
		>configure.log 2>&1) || { echo "$what configure failed: $OBJ/configure.log" >&2; exit 1; }
	printf '%s\n' "$key" > "$OBJ/.configured"
fi
# make's dependency tracking does not see localoptions.h; a changed header rebuilds everything.
[ "$OBJ/localoptions.h" -nt "$OBJ/.built" ] 2>/dev/null && make -C "$OBJ" clean >/dev/null 2>&1 || true
progs=
for p in "$@"; do progs="$progs ${p%%=*}"; done
if [ "$what" = dropbear ]; then
	make -C "$OBJ" -j"$(nproc)" PROGRAMS="${progs# }" >"$OBJ/make.log" 2>&1 ||
		{ echo "$what build failed: $OBJ/make.log" >&2; exit 1; }
else
	# shellcheck disable=SC2086
	make -C "$OBJ" -j"$(nproc)" $progs >"$OBJ/make.log" 2>&1 ||
		{ echo "$what build failed: $OBJ/make.log" >&2; exit 1; }
fi
touch "$OBJ/.built"
for p in "$@"; do
	out=$OUTDIR/${p#*=}
	"$STRIP" -o "$out.new" "$OBJ/${p%%=*}"
	if cmp -s "$out.new" "$out"; then rm -f "$out.new"; else mv -f "$out.new" "$out"; fi
done
