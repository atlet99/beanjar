#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
hookdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -x "$hookdir/pre-commit.inherited" ]; then
    "$hookdir/pre-commit.inherited" "$@"
fi
root=$(git rev-parse --show-toplevel)
export PRE_COMMIT_HOME="$root/build/pre-commit"
exec pre-commit hook-impl \
    --config "$root/.pre-commit-config.yaml" --hook-type pre-commit \
    --hook-dir "$hookdir" "$@"
