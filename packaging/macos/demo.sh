#!/bin/sh
# SPDX-FileCopyrightText: The Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
resources=$(cd "$(dirname "$0")" && pwd -P)
run=$(mktemp -d /tmp/beanjar-mac.XXXXXX)
printf 'Session files: %s\n' "$run"
ACCEL=hvf DISPLAY_BACKEND=cocoa GPU_RES=800x600 NET_LINK=off STATE=none SLOT=1 \
  FW="$resources/build/testguest/image" RUN_DIR="$run" \
  LOG_DIR="$run" IDENTITY_DIR="$run/identity" \
  "$resources/../MacOS/Beanjar" vm-launcher run
