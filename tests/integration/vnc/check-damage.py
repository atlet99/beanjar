#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-vnc's partial frames (docs/dev/architecture/vnc.md, "Read backs") against fake-qemu
--animate --bar (a grey band moving down the picture, only its rows updated), no VM, no window.

    check-damage.py BEANJAR_VNC

An RFB client on the --local socket takes Raw rectangles with continuous updates and draws them
into its own framebuffer, as a viewer does; beanjar-vnc composes, compares on the GPU and reads
back what its consumers need. Each check prints its numbers and passes or fails:

1. While the band moves, every picture the client holds between updates is fake-qemu's picture
   with exactly one band: BAR rows, contiguous, each one grey; and each RGB read back takes only
   the rows changed since its buffer's frame (the counter rgb_rows), at most a third of the
   picture on average.
2. Paused (beanjar-web's marker) while a video client streams (x264, NV12 from the GPU): frames
   are composed and none is read back as RGB (the counter rgb_reads) nor sent to the paused
   client; resumed, its next pictures are again the picture with one band.
3. With the animation stopped (the band's rows restored, only those updated), the client's
   framebuffer is fake-qemu's picture (within TOLERANCE in each channel), and so is a new
   client's first frame.
"""

import os
import signal
import socket
import subprocess
import sys
import tempfile
import time

TOP = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import loopback  # noqa: E402
from rfbclient import Rfb  # noqa: E402

VNC = sys.argv[1]
W, H, BAR = 640, 360, 20
BACKGROUND = (32, 128, 192)
RAW = [0, -313, -224]  # Raw, continuous updates, LastRect
PAUSE, RESUME = b"beanjar:rfb-pause", b"beanjar:rfb-resume"
# V3D's compose blends the white box's edge pixels a little (255, 254, 251): differences up to this
# are the same picture; a stale band is the bar's grey against blue or white
TOLERANCE = 8


def fail(message):
    sys.exit(f"check-damage: {message}")


def picture():
    """fake-qemu's picture (tests/fakes/fake-qemu.py picture()), as B, G, R, X rows."""
    bg = bytes((BACKGROUND[2], BACKGROUND[1], BACKGROUND[0]))
    x0, x1, y0, y1 = W * 3 // 8, W * 5 // 8, H * 3 // 8, H * 5 // 8
    rows = []
    for y in range(H):
        row = bytearray(bg * W)
        if y0 <= y < y1:
            row[x0 * 3 : x1 * 3] = b"\xff" * 3 * (x1 - x0)
        rows.append(bytes(row))
    return rows


def rgb_rows(fb):
    """The framebuffer's rows without the X bytes."""
    out = []
    for y in range(H):
        row = fb[y * W * 4 : (y + 1) * W * 4]
        out.append(bytes(b for i, b in enumerate(row) if i % 4 != 3))
    return out


def differing(fb, base):
    """The rows with a channel more than TOLERANCE away from base's."""
    return [
        y
        for y, (a, b) in enumerate(zip(rgb_rows(fb), base))
        if a != b and max(abs(p - q) for p, q in zip(a, b)) > TOLERANCE
    ]


def one_band(fb, base):
    """None if fb is base with one grey band of BAR rows, else what is wrong."""
    rows = differing(fb, base)
    if len(rows) != BAR or rows[-1] - rows[0] != BAR - 1:
        return f"{len(rows)} rows differ from the picture ({rows[:3]}...{rows[-3:]})"
    got = rgb_rows(fb)
    for y in rows:
        if max(got[y]) - min(got[y]) > TOLERANCE:
            return f"row {y} of the band is not one grey"
    return None


def counter(vnc, log_path, name):
    """One of beanjar-vnc's SIGUSR1 counters."""
    seen = open(log_path).read().count("beanjar-vnc: stats ")
    vnc.send_signal(signal.SIGUSR1)
    for _ in range(100):
        lines = [x for x in open(log_path).read().splitlines() if "beanjar-vnc: stats " in x]
        if len(lines) > seen:
            return int(dict(f.split("=", 1) for f in lines[-1].split()[2:])[name])
        time.sleep(0.02)
    fail("no stats line")


def wait_updates(c, n, timeout=5):
    start = c.picture()[1]
    end = time.monotonic() + timeout
    while c.picture()[1] < start + n and time.monotonic() < end:
        time.sleep(0.02)
    return c.picture()[1] - start


def main():
    d = tempfile.mkdtemp(prefix="check-damage-")
    fake = subprocess.Popen(
        [sys.executable, os.path.join(TOP, "tests/fakes/fake-qemu.py"), d, "--size", f"{W}x{H}",
         "--animate", "--bar"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )  # fmt: skip
    vnc = None
    clients = []
    base = picture()
    try:
        for _ in range(100):
            if os.path.exists(os.path.join(d, "ready")):
                break
            time.sleep(0.05)
        else:
            fail("fake-qemu did not start")
        local, video = os.path.join(d, "rfb.sock"), os.path.join(d, "video.sock")
        log = open(os.path.join(d, "vnc.log"), "w")
        vnc = subprocess.Popen(
            [VNC, f"unix:path={d}/dbus.sock", "--listen", loopback.ADDRESS, "--port",
             str(loopback.port()), "--size", f"{W}x{H}", "--fps", "30", "--no-media",
             "--native-cursor", "--local", local, "--video-local", video,
             "--video-encoder", "x264"],
            stdout=log, stderr=subprocess.STDOUT,
        )  # fmt: skip
        for _ in range(100):
            if "beanjar-vnc: ready" in open(log.name).read():
                break
            if vnc.poll() is not None:
                fail(f"beanjar-vnc exited: {open(log.name).read()}")
            time.sleep(0.1)
        a = Rfb(local, RAW, framebuffer=True)
        clients.append(a)
        if wait_updates(a, 1) < 1:
            fail("no first update")
        print(f"start: {len(differing(a.picture()[0], base))} rows differ from the picture")

        # 1. the band moving
        open(os.path.join(d, "animate"), "w").close()
        time.sleep(0.5)
        reads0, rows0 = counter(vnc, log.name, "rgb_reads"), counter(vnc, log.name, "rgb_rows")
        bad = 0
        for _ in range(10):
            time.sleep(0.15)
            fb, _n = a.picture()
            why = one_band(fb, base)
            if why:
                bad += 1
                print(f"moving: {why}")
        n = wait_updates(a, 5)
        reads = counter(vnc, log.name, "rgb_reads") - reads0
        rows = (counter(vnc, log.name, "rgb_rows") - rows0) / max(reads, 1)
        ok = not bad and n >= 5 and reads >= 10 and rows <= H // 3
        print(f"moving: 10 pictures, {10 - bad} with exactly one band; {n} updates in a moment; "
              f"{reads} RGB read backs of {rows:.0f} rows on average: "
              f"{'pass' if ok else 'FAIL'}")  # fmt: skip
        if not ok:
            fail("the moving band")

        # 2. paused while a video client streams, then resumed

        v = socket.socket(socket.AF_UNIX)
        v.connect(video)
        v.sendall(b"h264 bitrate=2000\n")
        v.settimeout(0.1)
        a.cut_text(PAUSE)
        time.sleep(0.2)
        before, renders0 = a.picture()[1], counter(vnc, log.name, "renders")
        reads0 = counter(vnc, log.name, "rgb_reads")
        end = time.monotonic() + 1.5
        while time.monotonic() < end:
            try:
                v.recv(1 << 20)
            except TimeoutError:
                pass
        during = a.picture()[1] - before
        renders = counter(vnc, log.name, "renders") - renders0
        reads = counter(vnc, log.name, "rgb_reads") - reads0
        a.cut_text(RESUME)
        n = wait_updates(a, 3)
        time.sleep(0.2)
        fb, _n = a.picture()
        why = one_band(fb, base)
        ok = not why and n >= 3 and during == 0 and renders >= 10 and reads == 0
        print(f"paused: {renders} frames composed for the video client, {reads} read back as RGB, "
              f"{during} updates to the "
              f"paused client; resumed: {n} updates, {why or 'the picture with one band'}: "
              f"{'pass' if ok else 'FAIL'}")  # fmt: skip
        if not ok:
            fail("the picture after a resume")
        v.close()

        # 3. stopped: the band's rows restored
        os.unlink(os.path.join(d, "animate"))
        time.sleep(1.0)
        rows = differing(a.picture()[0], base)
        b = Rfb(local, RAW, framebuffer=True)
        clients.append(b)
        wait_updates(b, 1)
        time.sleep(0.3)
        rows_b = differing(b.picture()[0], base)
        ok = not rows and not rows_b
        print(f"stopped: {len(rows)} rows differ in the first client, {len(rows_b)} in a new "
              f"one: {'pass' if ok else 'FAIL'}")  # fmt: skip
        if not ok:
            fail(f"stale rows after the band stopped: {rows[:5]} / {rows_b[:5]}")
    finally:
        for c in clients:
            c.close()
        if vnc and vnc.poll() is None:
            vnc.send_signal(signal.SIGTERM)
            vnc.wait(timeout=10)
        fake.send_signal(signal.SIGTERM)
        fake.wait(timeout=10)
        subprocess.run(["rm", "-rf", d])
    print("ok")


if __name__ == "__main__":
    main()
