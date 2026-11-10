#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Boot timeline from a saved log directory (build/logs/<name>, see guest/scripts/save-logs.sh).

Every time is guest uptime in seconds (CLOCK_MONOTONIC since the kernel started):
  - bootd.log times are bootd's clock; uptime = bootd time + the logged TimeDiff;
  - PmLog's messages carry UTC and the whole uptime second "[N]"; the UTC-to-uptime offset is
    the intersection of (utc - N - 1, utc - N] over all lines, normally well under 0.1 s wide;
  - dmesg lines carry uptime already.

  tests/regress/boot/timeline.py build/logs/<name> [--app APP_ID] [--json]
"""

import argparse
import datetime
import json
import re
import sys
from pathlib import Path

MSG = re.compile(r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d+)Z \[(\d+)\] (.*)$")


def utc(stamp):
    return (
        datetime.datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%S.%f")
        .replace(tzinfo=datetime.timezone.utc)
        .timestamp()
    )


def bootd_events(path):
    """[(uptime, text)] from bootd.log: PERFORMANCE lines and the waits between them."""
    if not path.exists():
        return [], None
    diff = 0.0
    events = []
    for line in path.read_text(errors="replace").splitlines():
        m = re.search(r"TimeDiff=([0-9.]+)", line)
        if m:
            diff = float(m.group(1))
            continue
        m = re.match(r"^(PERFORMANCE|DEBUG|INFO)\(\s*([0-9.]+)\) : (.*)$", line)
        if not m:
            continue
        kind, t, text = m.group(1), float(m.group(2)), m.group(3)
        if kind == "PERFORMANCE" or re.match(
            r"(action_waitForSignal|action_launchFirstApp|timer_launchFirstApp|action_launchApp="
            r"|callback_waitForCPUAvailable exit|action_waitForCPUAvailable|launch=request)",
            text,
        ):
            events.append((t, "bootd " + text))
    return [(t + diff, text) for t, text in events], diff


def pmlog(path):
    """(offset, [(uptime, text)]) from PmLog's messages."""
    if not path.exists():
        return None, []
    lines = []
    lo, hi = -1e18, 1e18
    for line in path.read_text(errors="replace").splitlines():
        m = MSG.match(line)
        if not m:
            continue
        t, n = utc(m.group(1)), int(m.group(2))
        lo, hi = max(lo, t - n - 1), min(hi, t - n)
        lines.append((t, m.group(3)))
    if not lines:
        return None, []
    offset = hi if lo > hi else (lo + hi) / 2  # lo > hi only if the clock stepped
    return offset, [(t - offset, text) for t, text in lines]


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("logdir", type=Path)
    parser.add_argument("--app", default="com.palm.app.settings", help="app whose launch is timed")
    parser.add_argument("--json", action="store_true", help="print only the summary as JSON")
    args = parser.parse_args()
    d = args.logdir

    events, timediff = bootd_events(d / "var-log/bootd.log")
    offset, messages = pmlog(d / "var-log/messages")
    summary = {"logdir": str(d), "bootd_timediff": timediff}

    dmesg = d / "dmesg.txt"
    if dmesg.exists():
        for line in dmesg.read_text(errors="replace").splitlines():
            m = re.match(r"^\[\s*([0-9.]+)\] \[init\] (starting (?:upstart|systemd).*)$", line)
            if m:
                events.append((float(m.group(1)), "init " + m.group(2)))
                summary["init"] = float(m.group(1))

    for t, text in events:
        m = re.match(r"bootd SIGNAL_(\S+)", text)
        if m and m.group(1) not in summary:
            summary[m.group(1)] = round(t, 2)

    app = args.app
    launch = None
    for t, text in messages:
        interesting = None
        if "LSM BOOT_STATUS" in text and '"BOOT_POPUP"' in text and '"bootCompleted": true' in text:
            interesting = "LSM launcher bootCompleted (launch points loaded)"
            summary.setdefault("launcher-ready", round(t, 2))
        elif (
            "SAM APPLAUNCH_BEGIN" in text
            or "SAM NATIVE_APPLAUNCHED" in text
            or "SAM WEB_APPLAUNCHED" in text
        ):
            interesting = text[:140]
            if f'"APP_ID":"{app}"' in text and "APPLAUNCH_BEGIN" in text and launch is None:
                launch = t
                summary["app-launch"] = round(t, 3)
        elif "surfaceMapped" in text:
            m = re.search(r'"([a-z0-9.]+)"\s*$', text.strip())
            interesting = "LSM surfaceMapped " + (m.group(1) if m else "?")
            if m and m.group(1) == app and launch is not None and "app-first-frame" not in summary:
                summary["app-first-frame"] = round(t, 3)
                summary["app-launch-to-first-frame"] = round(t - launch, 3)
        elif "crashd CRASHED" in text:
            interesting = text[:140]
        elif "WAM APPLAUNCH_START" in text:
            interesting = text[:140]
        elif "WAM APPLOADED" in text:
            interesting = text[:140]
            if f'"APP_ID":"{app}"' in text and launch is not None and "app-loaded" not in summary:
                summary["app-loaded"] = round(t, 3)
                summary["app-launch-to-loaded"] = round(t - launch, 3)
        if interesting:
            events.append((t, "pmlog " + interesting))
    summary["pmlog_offset"] = offset
    events.sort()
    if args.json:
        print(json.dumps(summary))
        return
    for t, text in events:
        print(f"{t:9.3f}  {text}")
    print(json.dumps(summary))


if __name__ == "__main__":
    sys.exit(main())
