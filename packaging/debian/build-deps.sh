#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the build-dependency metapackages (packaging/debian/build-deps/<role>.control) with
# equivs-build, so apt keeps their dependencies from autoremove.
#   packaging/debian/build-deps.sh [role...]     (default: every role)
# Output: build/packages/beanjar-build-deps-<role>_<version>_all.deb
# Version: 0.YYYYMMDDHHMMSS of the last commit that touched the control file, so a changed list
# is newer and apt upgrades it. SOURCE_DATE_EPOCH, when set (or without git history), replaces it.
# A dependency line "pkg [arch ...]," is kept only on those architectures (dpkg's own), as in
# Build-Depends: equivs takes no architecture restrictions in a binary package's Depends.
set -eu
TOP=$(cd "$(dirname "$0")/../.." && pwd)
SRC=$TOP/packaging/debian/build-deps
OUT=$TOP/build/packages

command -v equivs-build >/dev/null || { echo "build-deps.sh: equivs-build not found (apt install equivs)" >&2; exit 1; }

if [ $# -eq 0 ]; then
  set --
  for f in "$SRC"/*.control; do
    r=$(basename "$f" .control)
    set -- "$@" "$r"
  done
fi

ARCH=$(dpkg --print-architecture)
mkdir -p "$OUT"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

for role in "$@"; do
  ctl=$SRC/$role.control
  [ -f "$ctl" ] || { echo "build-deps.sh: no such role: $role" >&2; exit 1; }
  if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
    epoch=$SOURCE_DATE_EPOCH
  else
    epoch=$(git -C "$TOP" log -1 --format=%ct -- "packaging/debian/build-deps/$role.control" 2>/dev/null || true)
  fi
  [ -n "$epoch" ] || { echo "build-deps.sh: no commit for $ctl and SOURCE_DATE_EPOCH is unset" >&2; exit 1; }
  version=0.$(date -u -d "@$epoch" +%Y%m%d%H%M%S)
  { awk -v arch="$ARCH" '
      match($0, /\[[^]]*\]/) {
        n = split(substr($0, RSTART + 1, RLENGTH - 2), a, " "); keep = 0
        for (i = 1; i <= n; i++) if (a[i] == arch) keep = 1
        if (!keep) next
        $0 = substr($0, 1, RSTART - 1) substr($0, RSTART + RLENGTH); sub(/ +,/, ",")
      }
      { print }' "$ctl"; echo "Version: $version"; } > "$WORK/$role.control"
  (cd "$WORK" && equivs-build "$role.control" >"$WORK/log" 2>&1) || { cat "$WORK/log" >&2; exit 1; }
  mv "$WORK"/beanjar-build-deps-"$role"_*_all.deb "$OUT/"
  echo "$OUT/beanjar-build-deps-${role}_${version}_all.deb"
done
