#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-vnc answers the guest's capture (webos.capture, include/webos-capture.h) while idle:
no RFB or video client has ever come, so QEMU's GL scanout was never copied (wv_idle), and a
request must copy it first, as a bus Screenshot does. Against the stand-in QEMU display
(tests/fakes/fake-qemu.py --dmabuf, one buffer flipped for half a second), no VM, no window.

    check-capture.py BEANJAR_VNC

beanjar-vnc connects to --capture-socket (here a socket this test listens on, as QEMU's
chardev) and says hello; a BLENDED and an OSD request then each get the picture: the stand-in's
blue (32, 128, 192) at the bottom right, the white rectangle in the middle (sampled at its
corner: LG's cursor, which a capture has, is at the centre) and the cyan bar at the top left
(the right way up). Skipped (77) without a render node or libgbm.
"""

import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import loopback  # noqa: E402

TOP = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
VNC = sys.argv[1]
W, H = 640, 360
MAGIC = 0x50414357
BLENDED, OSD = 3, 4
BLUE, WHITE, CYAN = (32, 128, 192), (255, 255, 255), (0, 255, 255)
HEADER = struct.Struct("<6Iii")


def fail(message):
    sys.exit(f"check-capture: {message}")


def rx(conn, n):
    b = b""
    while len(b) < n:
        c = conn.recv(n - len(b))
        if not c:
            fail(f"the capture socket closed ({len(b)} of {n} bytes)")
        b += c
    return b


def request(conn, sequence, mode, w, h):
    """One request; its reply's status and RGBA pixels (top row first)."""
    conn.sendall(HEADER.pack(MAGIC, sequence, mode, w, h, 0, 0, 0))
    while True:
        magic, seq, rmode, rw, rh, size, status, _ = HEADER.unpack(rx(conn, HEADER.size))
        if magic != MAGIC:
            fail(f"a reply's magic {magic:#x}")
        if seq == 0:
            continue  # a repeated hello
        if (seq, rmode, rw, rh) != (sequence, mode, w, h):
            fail(f"the reply {seq} {rmode} {rw}x{rh} to {sequence} {mode} {w}x{h}")
        return status, rx(conn, size) if size else b""


def main():
    tmp = tempfile.mkdtemp(prefix="check-capture-")
    procs = []
    try:
        fake = subprocess.Popen([sys.executable, os.path.join(TOP, "tests/fakes/fake-qemu.py"), tmp,
                                 "--size", f"{W}x{H}", "--dmabuf", "--buffers", "1"])  # fmt: skip
        procs.append(fake)
        for _ in range(200):
            if os.path.exists(os.path.join(tmp, "ready")):
                break
            if fake.poll() is not None:
                if fake.returncode == 77:
                    sys.exit(77)
                fail(f"fake-qemu exited {fake.returncode}")
            time.sleep(0.05)
        else:
            fail("fake-qemu did not start")
        path = os.path.join(tmp, "capture.sock")
        server = socket.socket(socket.AF_UNIX)
        server.bind(path)
        server.listen(1)
        server.settimeout(10)
        log = os.path.join(tmp, "vnc.log")
        vnc = subprocess.Popen(
            [VNC, f"unix:path={tmp}/dbus.sock", "--listen", loopback.ADDRESS, "--port",
             str(loopback.port()), "--local", os.path.join(tmp, "rfb-local.sock"), "--size",
             f"{W}x{H}", "--no-media", "--name", "check-capture", "--capture-socket", path],
            stdout=open(log, "w"), stderr=subprocess.STDOUT)  # fmt: skip
        procs.append(vnc)
        # the stand-in's one frame (animate on: the first buffer, then the same one again)
        open(os.path.join(tmp, "animate"), "w").close()
        try:
            conn, _ = server.accept()
        except socket.timeout:
            fail(f"beanjar-vnc did not connect to the capture socket: {open(log).read()}")
        conn.settimeout(10)
        hello = HEADER.unpack(rx(conn, HEADER.size))
        if hello[0] != MAGIC or hello[1] != 0 or hello[6] != 0:
            fail(f"the hello: {hello}")
        for _ in range(100):
            if "beanjar-vnc: ready" in open(log).read():
                break
            time.sleep(0.1)
        else:
            fail("beanjar-vnc not ready")
        time.sleep(0.5)  # frames flipped while idle: acknowledged, not copied
        os.unlink(os.path.join(tmp, "animate"))
        for sequence, mode in enumerate((BLENDED, OSD), 1):
            status, pixels = request(conn, sequence, mode, W, H)
            if status:
                fail(f"mode {mode} while idle: status {status} ({os.strerror(-status)})")

            def at(x, y, pixels=pixels):
                return tuple(pixels[(y * W + x) * 4 : (y * W + x) * 4 + 3])

            got = (at(W - 5, H - 5), at(W * 5 // 8 - 4, H * 5 // 8 - 4), at(5, 5))
            if got != (BLUE, WHITE, CYAN):
                fail(f"mode {mode}: bottom right, rectangle, top left {got}")
            print(f"mode {mode}: the picture while idle, corner {at(W - 5, H - 5)}")
        conn.close()
        print("ok")
    finally:
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
        subprocess.run(["rm", "-rf", tmp])


if __name__ == "__main__":
    main()
