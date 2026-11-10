#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-vnc's picture rate from a steady GL source, with no guest: the stand-in QEMU display
(tests/fakes/fake-qemu.py --dmabuf) flips GBM buffers at --rate frames a second and waits on
each UpdateDMABUF's answer as QEMU does, while an RFB client on the --local socket asks for
updates back to back (Open H.264 where neatvnc has an encoder, else Tight).

    check-rate.py BEANJAR_VNC [--rate N] [--seconds S] [--size WxH] [--out WxH] [--client rfb|tight|none]

It prints one JSON line with every stage's rate a second: the source's frames (and the ones it
had to hold back, late), the updates beanjar-vnc got, its copies, renders and changed renders,
the frames fed to neatvnc, the client's updates, the source's wait per frame for the answer and
the renders' GPU time. It passes when frames went through every stage; the rates themselves are
the host's and are reported, not judged. Skipped (77) without a render node or libgbm.
"""

import argparse
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import loopback  # noqa: E402

TOP = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
FAKE = os.path.join(TOP, "tests", "fakes", "fake-qemu.py")


def fail(msg):
    print(f"check-rate: {msg}", file=sys.stderr)
    sys.exit(1)


class Rfb:
    """An RFB client (security None) counting framebuffer updates, each requested at once."""

    def __init__(self, path, h264=True):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(path)
        self.f = self.s.makefile("rwb", buffering=0)
        self.updates = 0
        self.encodings = set()
        self.rd(12)
        self.f.write(b"RFB 003.008\n")
        n = self.rd(1)[0]
        if 1 not in self.rd(n):
            fail("the local socket asks for authentication")
        self.f.write(b"\x01")
        if struct.unpack(">I", self.rd(4))[0]:
            fail("security None refused")
        self.f.write(b"\x01")
        self.w, self.h = struct.unpack(">HH", self.rd(4))
        self.rd(16)
        self.rd(struct.unpack(">I", self.rd(4))[0])
        encs = ([50] if h264 else []) + [7, 0]
        self.f.write(struct.pack(">BxH", 2, len(encs)) + struct.pack(f">{len(encs)}i", *encs))
        self.request(0)

    def rd(self, n):
        b = b""
        while len(b) < n:
            c = self.f.read(n - len(b))
            if not c:
                raise EOFError
            b += c
        return b

    def request(self, inc):
        self.f.write(struct.pack(">BBHHHH", 3, inc, 0, 0, self.w, self.h))

    def compact(self):
        n = 0
        for i in range(3):
            b = self.rd(1)[0]
            n |= (b & 0x7F) << (7 * i)
            if not b & 0x80:
                break
        return n

    def run(self, stop):
        try:
            while not stop.is_set():
                self.message()
        except (EOFError, OSError, ValueError):
            pass

    def message(self):
        mt = self.rd(1)[0]
        if mt == 0:
            self.rd(1)
            for _ in range(struct.unpack(">H", self.rd(2))[0]):
                _x, _y, rw, rh, enc = struct.unpack(">HHHHi", self.rd(12))
                self.encodings.add(enc)
                if enc == 50:  # Open H.264: length, flags, the data
                    self.rd(struct.unpack(">II", self.rd(8))[0])
                elif enc == 7:  # Tight
                    c = self.rd(1)[0]
                    if c & 0xF0 == 0x90:
                        self.rd(self.compact())
                    elif c & 0xF0 == 0x80:
                        self.rd(3)
                    else:
                        sz = rw * rh * 3
                        self.rd(sz if sz < 12 else self.compact())
                elif enc == 0:
                    self.rd(rw * rh * 4)
                elif enc not in (-223, -308):
                    raise ValueError(f"encoding {enc}")
            self.updates += 1
            self.request(1)
        elif mt == 3:
            self.rd(3)
            self.rd(struct.unpack(">I", self.rd(4))[0])
        elif mt != 2:
            raise ValueError(f"message {mt}")


def stats(vnc, log):
    """beanjar-vnc's SIGUSR1 line, as a dict of ints."""
    size = os.path.getsize(log)
    vnc.send_signal(signal.SIGUSR1)
    for _ in range(100):
        time.sleep(0.02)
        with open(log) as f:
            f.seek(size)
            lines = [x for x in f.read().splitlines() if "beanjar-vnc: stats " in x]
        if lines:
            return {k: int(v) for k, v in (x.split("=", 1) for x in lines[-1].split()[2:])}
    fail("no stats line on SIGUSR1")


def source(d):
    with open(os.path.join(d, "dmabuf.stats")) as f:
        return {k: int(v) for k, v in (x.split("=", 1) for x in f.read().split())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vnc")
    ap.add_argument("--rate", type=int, default=60)
    ap.add_argument("--seconds", type=float, default=5)
    ap.add_argument("--warm", type=float, default=2)
    ap.add_argument("--size", default="1920x1080", help="the source's picture")
    ap.add_argument("--out", default="1280x720", help="beanjar-vnc's --size")
    ap.add_argument("--client", default="rfb", choices=("rfb", "tight", "none"))
    # tight: no Open H.264, so no hardware encoder (the VEPU) runs; rfb asks for it where offered
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    d = tempfile.mkdtemp(prefix="check-rate-")
    procs = []
    try:
        fake_log = open(os.path.join(d, "fake-qemu.log"), "w")
        fake = subprocess.Popen([sys.executable, FAKE, d, "--size", a.size, "--dmabuf", "--rate",
                                 str(a.rate)], stderr=fake_log)  # fmt: skip
        procs.append(fake)
        for _ in range(200):
            if os.path.exists(os.path.join(d, "ready")):
                break
            if fake.poll() is not None:
                err = open(fake_log.name).read()
                if fake.returncode == 77:
                    print(f"check-rate: skipped: {err.strip().splitlines()[-1]}")
                    sys.exit(77)
                fail(f"fake-qemu exited {fake.returncode}: {err}")
            time.sleep(0.05)
        else:
            fail("fake-qemu did not start")
        log = os.path.join(d, "vnc.log")
        local = os.path.join(d, "rfb-local.sock")
        with open(log, "w") as out:
            vnc = subprocess.Popen(
                [a.vnc, f"unix:path={d}/dbus.sock", "--listen", loopback.ADDRESS, "--port", str(loopback.port()),
                 "--size", a.out, "--fps", "60", "--no-media", "--local", local],
                stdout=out, stderr=subprocess.STDOUT,
            )  # fmt: skip
        procs.append(vnc)
        for _ in range(200):
            if "beanjar-vnc: ready" in open(log).read():
                break
            if vnc.poll() is not None:
                fail(f"beanjar-vnc exited {vnc.returncode}: {open(log).read()[-800:]}")
            time.sleep(0.05)
        else:
            fail("beanjar-vnc not ready")
        open(os.path.join(d, "animate"), "w").close()
        stop = threading.Event()
        client = None
        if a.client in ("rfb", "tight"):
            client = Rfb(local, a.client == "rfb")
            threading.Thread(target=client.run, args=(stop,), daemon=True).start()
        time.sleep(a.warm)
        s0, f0, u0 = stats(vnc, log), source(d), client.updates if client else 0
        time.sleep(a.seconds)
        s1, f1, u1 = stats(vnc, log), source(d), client.updates if client else 0
        stop.set()
        if vnc.poll() is not None:
            fail(f"beanjar-vnc exited {vnc.returncode}: {open(log).read()[-800:]}")
        dt = (s1["t_us"] - s0["t_us"]) / 1e6
        ds = {k: s1[k] - s0[k] for k in s1}
        df = {k: f1[k] - f0[k] for k in f1}
        r = lambda n: round(n / dt, 1)
        out = {
            "label": a.label,
            "rate": a.rate,
            "size": a.size,
            "out": a.out,
            "seconds": round(dt, 2),
            "source_frames_per_s": r(df["frames"]),
            "source_late_per_s": r(df["late"]),
            "source_wait_ms": round(df["waited_us"] / max(df["frames"], 1) / 1000, 2),
            "updates_per_s": r(ds["updates"]),
            "copies_per_s": r(ds["copies"]),
            "renders_per_s": r(ds["renders"]),
            "changed_per_s": r(ds["renders"] - ds["unchanged"]),
            "feeds_per_s": r(ds["feeds"]),
            "client_updates_per_s": r(u1 - u0),
            "client_encodings": sorted(client.encodings) if client else [],
            "reply_ms": round(ds["reply_us"] / max(ds["updates"], 1) / 1000, 2),
            "render_ms": round(ds["render_us"] / max(ds["renders"], 1) / 1000, 2),
        }
        print(json.dumps(out))
        if not df["frames"] or not ds["updates"]:
            fail("the source's frames stopped before the end")
        if client and (not ds["copies"] or not ds["renders"]):
            fail("frames were not copied or rendered for the client")
        if client and u1 == u0:
            fail("the RFB client got no updates")
        print("ok")
    finally:
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(10)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
        subprocess.run(["rm", "-rf", d], check=False)


if __name__ == "__main__":
    main()
