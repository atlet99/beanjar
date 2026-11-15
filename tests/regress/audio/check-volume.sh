#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# End-to-end volume check on the QEMU host, with a VM booted with DISPLAY_BACKEND=dbus (AUDIO=dbus)
# and the viewer built (the host build). Records the guest through the viewer while
# tests/regress/audio/guest-volume.sh plays a 1 kHz tone at master volumes 100, 50, 10, muted and 11,
# set through audiod (com.webos.audio -> tvservice's tv.sound -> its sound engine volume, which the stock HAL sends to lg115x.ko's audio master and lgemu.ko applies as the card's playback gain). The level is LG's volume curve
# (the model's table at gpGainAudioOUTPUT+32):
# volume 100 is 0 dB, 50 -12.31 dB, 10 -35 dB, 11 -33 dB; so the tone must come back at those
# levels relative to volume 100, and not at all while muted.
#   tests/regress/audio/check-volume.sh            (from the repository root; leaves volume at 11)
set -eu
cd "$(dirname "$0")/../../.."
# The tone goes to the VM's output share (build/share/out, the guest's /mnt/host/out), through
# put-out, which never writes through a link the guest made there; the guest half into its
# read-only share.
python3 tests/regress/audio/check-audio.py --make-tone build/check-volume-tone.wav --seconds 3 --level -12 >/dev/null
host/scripts/vm-launcher put-out build/check-volume-tone.wav tone-1k-3s.wav >/dev/null
host/scripts/vm-launcher put tests/regress/audio/guest-volume.sh >/dev/null
rm -f build/check-volume.wav
build/viewer/beanjar-viewer "unix:path=$PWD/build/dbus.sock" --headless --seconds 42 \
	--audio-wav build/check-volume.wav > build/check-volume-viewer.log 2>&1 &
viewer=$!
sleep 2
host/scripts/vm-launcher console "webos-enter sh /mnt/host/tests/regress/audio/guest-volume.sh /mnt/host/out/tone-1k-3s.wav" 32 |
	tee build/check-volume-guest.log | grep -E '^(play|\{)' || true
wait $viewer || true
grep '^audio:' build/check-volume-viewer.log
python3 tests/regress/audio/check-audio.py build/check-volume.wav --min-rms -60 --max-gap 1e9 --bursts 300 |
	tee build/check-volume.out
python3 - build/check-volume.out <<'PY'
import re, sys
levels = [float(m) for m in re.findall(r"^burst \d+: .*?, ([-\d.]+) dBFS", open(sys.argv[1]).read(), re.M)]
want = [0, -12.31, -35.0, -33.0]
ok = len(levels) == len(want) and all(abs(l - levels[0] - w) <= 0.5 for l, w in zip(levels, want))
print(("PASS" if ok else "FAIL") + " volume: relative levels " +
      " ".join(f"{l - levels[0]:.2f}" for l in levels) + " dB (expected " + " ".join(map(str, want)) + ")")
sys.exit(0 if ok else 1)
PY
