#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""--hide-planes: the planes a front end leaves out of its picture (host/viewer/core/core.c,
wv_planes).

    check-planes.py VIEWER_HEADLESS

Against tests/fakes/fake-qemu.py (an opaque blue picture with a white rectangle in the middle,
LG's 16x16 cursor at the centre) and a stand-in webos.video port (one shown MAIN window at 480,270
960x540 of 1920x1080), with --no-media so that the video window is the checkerboard,
viewer-headless takes one screenshot per case and the test reads three points: a corner (the UI
alone), a point in the window beside the cursor, and the cursor.

  - nothing hidden: the UI's blue, its white over the window, the cursor;
  - cursor: the cursor's point is the UI's white;
  - ui: black in the corner, the checkerboard in the window, the cursor;
  - video: the UI as before, and the window never drawn (video_draws 0);
  - video,ui,cursor: black everywhere;
  - a bad list ("planes"): refused, exit status not 0.

That the bus's Screenshot still gets every plane is tests/integration/vnc/check-planes.py's (headless does not
serve org.beanjar.Display1)."""

import importlib.util
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cvp", os.path.join(HERE, "check-video-plane.py"))
cvp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cvp)

BLUE_UI, WHITE, CURSOR, BLACK = (32, 128, 192), (255, 255, 255), (220, 80, 20), (0, 0, 0)
CHECKER = "checkerboard"


def pixel(path, fx, fy):
    w, h, px = cvp.read_ppm(path)
    x, y = int(w * fx), int(h * fy)
    return tuple(px[(y * w + x) * 3 : (y * w + x) * 3 + 3])


def near(p, c, tol=12):
    return all(abs(p[i] - c[i]) <= tol for i in range(3))


def classify(p):
    for name, c in (("blue", BLUE_UI), ("white", WHITE), ("cursor", CURSOR), ("black", BLACK)):
        if near(p, c):
            return name
    if near(p, cvp.LIGHT) or near(p, cvp.BLUE):
        return CHECKER
    return f"other{p}"


# corner (0.05, 0.05); window beside the cursor, in the UI's white (0.45, 0.45); the cursor (its 16x16 at 960,540
# of 1920x1080 is 5x5 pixels of the 640x360 shot, from the centre)
POINTS = ((0.05, 0.05), (0.45, 0.45), (0.503, 0.503))
CASES = (
    ("nothing hidden", None, ("blue", "white", "cursor"), True),
    ("cursor", "cursor", ("blue", "white", "white"), True),
    ("ui", "ui", ("black", CHECKER, "cursor"), True),
    ("video", "video", ("blue", "white", "cursor"), False),
    ("all", "video,ui,cursor", ("black", "black", "black"), False),
)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    viewer = sys.argv[1]
    tmp = tempfile.mkdtemp(prefix="check-planes-")
    run = os.path.join(tmp, "run")
    qemu = subprocess.Popen(
        [sys.executable, os.path.join(HERE, "..", "..", "fakes", "fake-qemu.py"), run]
    )
    stop = threading.Event()
    failed = []
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen")
    bus = f"unix:path={os.path.join(run, 'dbus.sock')}"
    base = [viewer, bus, "--no-audio", "--no-media", "--size", "640x360"]
    thread = None
    try:
        for _ in range(200):
            if os.path.exists(os.path.join(run, "ready")):
                break
            time.sleep(0.05)
        else:
            print("FAIL: fake-qemu did not start")
            return 1
        vs = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        vs.bind(os.path.join(run, "video.sock"))
        vs.listen(4)
        thread = threading.Thread(target=cvp.serve, args=(vs, cvp.video_handler, stop), daemon=True)
        thread.start()
        for n, (what, hide, want, drawn) in enumerate(CASES):
            shot = os.path.join(tmp, f"shot-{n}.ppm")
            extra = ["--hide-planes", hide] if hide else []
            r = subprocess.run(
                base + ["--seconds", "2", "--screenshot", shot, *extra],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=60,
                env=env,
            )
            if r.returncode or not os.path.exists(shot):
                failed.append(f"{what}: viewer exit {r.returncode}: {r.stdout[-2000:]}")
                continue
            got = tuple(classify(pixel(shot, *p)) for p in POINTS)
            draws = int(cvp.re.search(r"video_draws=(\d+)", r.stdout).group(1))
            print(f"{what}: corner {got[0]}, window {got[1]}, cursor {got[2]}; video_draws {draws}")
            if got != want:
                failed.append(f"{what}: want {want}, got {got}")
            if bool(draws) != drawn:
                failed.append(f"{what}: video_draws {draws}, want {'some' if drawn else 'none'}")
        r = subprocess.run(
            base + ["--seconds", "1", "--hide-planes", "planes"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=60,
            env=env,
        )
        print(f"bad list: exit {r.returncode}")
        if r.returncode == 0:
            failed.append("--hide-planes planes was accepted")
    finally:
        stop.set()
        if thread:
            thread.join()
        qemu.terminate()
        try:
            qemu.wait(10)
        except subprocess.TimeoutExpired:
            qemu.kill()
            qemu.wait()
        shutil.rmtree(tmp, ignore_errors=True)
    for f in failed:
        print(f"FAIL: {f}")
    if failed:
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
