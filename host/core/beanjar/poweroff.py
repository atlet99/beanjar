# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""A running guest's state as QMP and its console tell it (qmp_status, console_booted),
and the power-off the library asks of it."""

import json
import os
import re
import socket


def qmp_status(path, timeout=1.0):
    """QMP query-status on path: the run state ("running", "paused", "shutdown", ...), or None if
    QMP does not answer (no socket, or another client holds it: QEMU serves one at a time). A running
    guest whose micom is in standby (Quick Start+, or suspended; the guest holds its webos.standby
    port open, qemu.py) is "standby"."""
    if not os.path.exists(path):
        return None
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(timeout)
    try:
        s.connect(path)
        f = s.makefile("rw")
        json.loads(f.readline())  # greeting
        replies = {}
        for cmd in ("qmp_capabilities", "query-status", "query-chardev"):
            f.write(json.dumps({"execute": cmd}) + "\n")
            f.flush()
            while True:
                reply = json.loads(f.readline())
                if "return" in reply or "error" in reply:
                    break
            replies[cmd] = reply.get("return")
        status = (replies["query-status"] or {}).get("status")
        if status == "running" and any(
            c.get("label") == "standby" and c.get("frontend-open")
            for c in replies["query-chardev"] or []
        ):
            return "standby"
        return status
    except (OSError, ValueError):
        return None
    finally:
        s.close()


# How long the power-off signal waits for QEMU to exit by itself, from the call, before the stop
# syncs and stops the VM instead (or, without the fallback, gives up): webOS's shutdown took 7 to
# 13 s on the Pi's KVM, and on the PC's TCG 5 s (WT1H) and 30 s (W22O's webOS 23).
POWER_OFF_TIMEOUT = 90


# Typed on the initramfs console (webos-enter: inside webOS). luna-send reads its stdin, so it gets
# /dev/null; timeout bounds a hub that does not answer, which would hold the console's shell. On the
# PC's TCG a busy webOS 23 took over 10 s to answer.
# tvpowerd's powerOff with cpuCommand, a force reason on every webOS release: the set's own
# shutdown, never Quick Start+'s standby.
POWER_OFF_CALL = (
    "timeout 30 webos-enter luna-send -n 1 luna://com.webos.service.tvpower/power/powerOff "
    '\'{"reason":"cpuCommand"}\' </dev/null'
)


POWER_OFF_SENT = "power-off"  # run/power-off: QEMU's pid, once the signal was sent to it


MICOM_STATUS = "micom-status"  # run/micom-status: the guest's micom state, a line at each change


def power_off_refused(reply):
    """Whether the console's text after POWER_OFF_CALL says webOS cannot take the signal: tvpowerd
    answered "returnValue": false, luna-send reports an error (the service does not exist, or it
    cannot connect to the hub: W21P's hub once stopped taking connections, the call never reached
    tvpowerd and no shutdown ran), or webOS is not running. No reply, or a luna-send that timed out, is not a refusal: webOS has been
    seen to power off after a call that never answered."""
    flat = "".join(reply.split())
    return bool(
        re.search(r'"returnValue":false', flat)
        or "webOSnamespaceisnotrunning" in flat
        or re.search(r"Servicedoesnotexist|ServiceUnknown|:notfound|Nosuchfile", flat)
        or "Unabletoconnecttocom.palm.hub" in flat
    )


def power_off_busy(reply):
    """Whether the refusal is tvpowerd's "com.webos.service.tvpower/power is busy" (W21P once
    answered so after about 49 h of uptime, and accepted a later stop): the call is retried."""
    return power_off_refused(reply) and "powerisbusy" in "".join(reply.split())


def micom_ready(run_dir):
    """Whether the guest's firmware has started its power stack: the last line of run/micom-status
    has ready=1 (include/webos-ir.h: lgemu's micom saw the firmware read the power state, which
    its power stack does as it starts, a moment before tvpowerd answers). False without a line."""
    try:
        with open(os.path.join(run_dir, MICOM_STATUS), "rb") as f:
            f.seek(0, os.SEEK_END)
            f.seek(max(0, f.tell() - 1024))
            lines = f.read().decode("ascii", "replace").splitlines()
    except OSError:
        return False
    return bool(lines) and "ready=1" in lines[-1].split()


BOOT_MARK = b"debug shell"


_booted = {}  # console log path -> (device, inode, bytes read, booted)


def console_booted(path):
    """The guest got as far as its debug shell (BOOT_MARK in its console log). Read once: what was
    read of the log is remembered, and a refresh reads only what was added since (the window asks
    every second, and a running VM's log grows to megabytes)."""
    try:
        with open(path, "rb") as f:
            st = os.fstat(f.fileno())
            dev, ino, done, booted = _booted.get(path, (None, None, 0, False))
            if (dev, ino) != (st.st_dev, st.st_ino) or st.st_size < done:
                done, booted = 0, False  # a new log (the VM started again)
            if not booted and st.st_size > done:
                f.seek(max(0, done - len(BOOT_MARK) + 1))
                data = f.read(st.st_size - f.tell())
                booted = BOOT_MARK in data
                done = f.tell()
            _booted[path] = (st.st_dev, st.st_ino, done, booted)
            return booted
    except OSError:
        _booted.pop(path, None)
        return False
