#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Inside webOS: the media path's log lines (video milestone 1), from PmLog (/var/log/messages) and plain syslog (/var/log/legacy-log); not time-merged.
#   (the VM gets it with `host/scripts/vm-launcher put tests/regress/video/guest-log.sh` on the host)
#   webos-enter sh /mnt/host/tests/regress/video/guest-log.sh [PATTERN] [LINES]
pattern=${1:-'tvservices-stub: (video|videosinkmanager|tv\.display|tv\.avblock)|vdecsink-stub|adecsink|media-port|vsm register|loadCompleted|umsmediainfo|error\[|seekDone|playing'}
grep -a -h -E "$pattern" /var/log/messages /var/log/legacy-log | cut -c1-${COLS:-300} | tail -n "${2:-60}"
