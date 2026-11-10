#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# ldd -r equivalent for glibc 2.14 ld.so: resolve every symbol now, print only problems
for f in "$@"; do
  out=$(LD_TRACE_LOADED_OBJECTS=1 LD_WARN=yes LD_BIND_NOW=yes /lib/ld-linux.so.3 "$f" 2>&1)
  gl=$(echo "$out" | grep -E 'lib(EGL|GLESv2|GLESv1_CM|wayland-(egl|client|server))\.so' | awk '{print $1"=>"$3}' | tr '\n' ' ')
  bad=$(echo "$out" | grep -iE 'undefined|not found|error' | head -5)
  echo "$f: ${bad:-OK} [$gl]"
done
