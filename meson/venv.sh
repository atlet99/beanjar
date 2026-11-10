#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Make or refresh a Python venv from a hash-pinned requirements file, in place:
#
#   meson/venv.sh VENV REQUIREMENTS [STAMP]
#
# A venv cannot be moved (its scripts name their interpreter by absolute path), so it is built
# where it is used. VENV/.requirements records the requirements it was made from and the
# interpreter's minor version (a venv's site-packages belong to one, so an OS upgrade of python3
# breaks it); when both match, nothing happens. When they differ, a venv that already holds
# exactly the pinned versions (pip freeze) is kept and only marked; otherwise the old venv is
# moved aside to VENV.old (a running mitmweb keeps working until the next change) and a new one
# is made with pip install --require-hashes. STAMP (for Meson) is rewritten only when VENV changed.
set -eu
VENV=$1 REQ=$2 STAMP=${3:-}
PY=${PYTHON:-python3}
want="$(sha256sum "$REQ" | cut -d' ' -f1) python$("$PY" -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
pins() { sed -n 's/^\([A-Za-z0-9_.-]*==[^ ;\\]*\).*/\1/p' "$REQ" | tr 'A-Z_' 'a-z-' | sort; }
freeze() { "$VENV/bin/python" -m pip freeze --all 2>/dev/null | grep -v -E '^(pip|setuptools|wheel)==' |
	tr 'A-Z_' 'a-z-' | sort; }

if [ "$(cat "$VENV/.requirements" 2>/dev/null)" != "$want" ]; then
	if [ -x "$VENV/bin/python" ] && [ "$(freeze)" = "$(pins)" ]; then
		echo "$VENV: already has the pinned versions" >&2
	else
		if [ -e "$VENV" ]; then rm -rf "$VENV.old"; mv "$VENV" "$VENV.old"; fi
		"$PY" -m venv "$VENV"
		"$VENV/bin/python" -m pip install -q --disable-pip-version-check --require-hashes -r "$REQ"
		echo "$VENV: made from $REQ" >&2
	fi
	echo "$want" > "$VENV/.requirements"
fi
[ -z "$STAMP" ] || { [ "$(cat "$STAMP" 2>/dev/null)" = "$want" ] || echo "$want" > "$STAMP"; }
