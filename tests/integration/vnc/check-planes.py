#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-vnc's planes (docs/dev/architecture/vnc.md, "Planes") against fake-qemu (the blue picture
with a white box in the middle, LG's cursor at the centre), no VM, no window.

    check-planes.py BEANJAR_VNC

Two RFB clients on the --local socket, as two web viewers' pages; each check prints what it saw
and passes or fails:

1. "beanjar:planes?" is answered to the clients with "beanjar:planes=0".
2. "beanjar:planes=2" (the UI hidden) from one client: both get "beanjar:planes=2", and both
   pictures turn black where the UI was (no video window is shown).
3. With the UI hidden, Display1.Screenshot (`beanjar screenshot`) still has every plane, at the
   desktop's size (the core's own) and at 384x216 (beanjar-vnc's thumbnail): blue in the corner.
4. A marker out of range ("beanjar:planes=9") changes nothing and is not answered.
5. "beanjar:planes=0": the UI back in both pictures.
"""

import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import loopback  # noqa: E402
from rfbclient import Rfb  # noqa: E402

TOP = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
VNC = sys.argv[1]
W, H = 640, 360
RAW = [0, -313, -224]  # Raw, continuous updates, LastRect
failures = []


def check(ok, what):
    print(f"{'pass' if ok else 'FAIL'}: {what}")
    if not ok:
        failures.append(what)


def corner(c):
    """The client's picture at 20,20 as R, G, B (its framebuffer is B, G, R, X)."""
    fb, _ = c.picture()
    o = (20 * W + 20) * 4
    return fb[o + 2], fb[o + 1], fb[o]


def near(p, q, tol=8):
    return all(abs(a - b) <= tol for a, b in zip(p, q))


def wait(pred, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if pred():
            return True
        time.sleep(0.05)
    return pred()


def screenshot_corner(d, w, h):
    path = os.path.join(d, f"shot-{w}x{h}.ppm")
    r = subprocess.run(
        ["gdbus", "call", "--address", f"unix:path={d}/dbus.sock", "--dest",
         "org.beanjar.Display1", "--object-path", "/org/beanjar/Display1", "--method",
         "org.beanjar.Display1.Screenshot", path, str(w), str(h)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=20,
    )  # fmt: skip
    if r.returncode:
        return f"call failed: {r.stdout.strip()}"
    data = open(path, "rb").read()
    head = data.split(b"\n", 3)
    pw = int(head[1].split()[0])
    px = head[3]
    x, y = w // 32, h // 18
    o = (y * pw + x) * 3
    return tuple(px[o : o + 3])


def main():
    d = tempfile.mkdtemp(prefix="check-vnc-planes-")
    fake = subprocess.Popen(
        [sys.executable, os.path.join(TOP, "tests/fakes/fake-qemu.py"), d, "--size", f"{W}x{H}"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )  # fmt: skip
    vnc = None
    clients = []
    try:
        if not wait(lambda: os.path.exists(os.path.join(d, "ready")), 10):
            sys.exit("check-vnc-planes: fake-qemu did not start")
        local = os.path.join(d, "rfb.sock")
        log = open(os.path.join(d, "vnc.log"), "w")
        vnc = subprocess.Popen(
            [VNC, f"unix:path={d}/dbus.sock", "--listen", loopback.ADDRESS, "--port",
             str(loopback.port()), "--size", f"{W}x{H}", "--fps", "30", "--no-media",
             "--local", local],
            stdout=log, stderr=subprocess.STDOUT,
        )  # fmt: skip
        if not wait(lambda: "beanjar-vnc: ready" in open(log.name).read(), 10):
            sys.exit(f"check-vnc-planes: beanjar-vnc not ready: {open(log.name).read()}")
        a, b = Rfb(local, RAW, framebuffer=True), Rfb(local, RAW, framebuffer=True)
        clients += [a, b]
        blue = (32, 128, 192)
        check(wait(lambda: near(corner(a), blue) and near(corner(b), blue)),
              f"both pictures start with the UI: {corner(a)}, {corner(b)}")  # fmt: skip

        a.cut_text(b"beanjar:planes?")
        check(wait(lambda: b"beanjar:planes=0" in a.cut_texts and b"beanjar:planes=0" in b.cut_texts),
              f"asked: answered beanjar:planes=0 ({a.cut_texts}, {b.cut_texts})")  # fmt: skip

        a.cut_text(b"beanjar:planes=2")
        check(wait(lambda: b"beanjar:planes=2" in b.cut_texts),
              f"UI hidden by one client: the other told ({b.cut_texts})")  # fmt: skip
        check(wait(lambda: near(corner(a), (0, 0, 0)) and near(corner(b), (0, 0, 0))),
              f"UI hidden: both pictures black where it was: {corner(a)}, {corner(b)}")  # fmt: skip

        for w, h in ((W, H), (384, 216)):
            got = screenshot_corner(d, w, h)
            check(isinstance(got, tuple) and near(got, blue),
                  f"UI hidden: Display1.Screenshot at {w}x{h} has every plane: corner {got}")  # fmt: skip
        check(wait(lambda: near(corner(a), (0, 0, 0)), 2),
              f"after the screenshots the clients' picture is still without the UI: {corner(a)}")  # fmt: skip

        n = len(b.cut_texts)
        a.cut_text(b"beanjar:planes=9")
        time.sleep(0.5)
        check(len(b.cut_texts) == n and near(corner(b), (0, 0, 0)),
              f"beanjar:planes=9 ignored ({b.cut_texts[n:]})")  # fmt: skip

        b.cut_text(b"beanjar:planes=0")
        check(wait(lambda: near(corner(a), blue) and near(corner(b), blue)),
              f"planes=0: the UI back in both: {corner(a)}, {corner(b)}")  # fmt: skip
        check("planes hidden 2" in open(log.name).read(), "beanjar-vnc logged the change")
    finally:
        for c in clients:
            c.close()
        if vnc:
            vnc.terminate()
            vnc.wait(10)
        fake.terminate()
        fake.wait(10)
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
