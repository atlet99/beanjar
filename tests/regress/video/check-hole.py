#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the video hole in a viewer capture (video milestone 1).

The viewer (host/viewer, --screenshot OUT.ppm --osd-alpha) writes the composed picture OUT.ppm and
the guest's OSD as it arrived, with alpha, OUT.ppm.osd.pam. The video shows only where the OSD is
transparent, so for a media window RECT (screen pixels, from setDisplayWindow) this reports:

  - the OSD alpha inside RECT (the hole: WebKit's page, WAM's surface, the compositor and the
    scanout together) and outside it, with a few sample points;
  - the share of the composed picture inside RECT that shows the viewer's checkerboard (decoding
    off: run the viewer with --no-media; with decoding on a window without a picture is black).

  check-hole.py OUT.ppm RECT [--min-hole 0.9] [--min-pattern 0.5]
  RECT is x,y,w,h in the OSD's pixels (1920x1080), e.g. 0,0,1920,1080.
Exit status 0 when both shares reach their minimum.
"""

import argparse
import sys

from PIL import Image

# host/viewer/core/video.c: light squares (.85) and dark blue ones (.12,.12,.40)
LIGHT = (217, 217, 217)
BLUE = (31, 31, 102)


def is_pattern(p):
    return all(abs(a - b) <= 12 for a, b in zip(p, LIGHT)) or all(
        abs(a - b) <= 12 for a, b in zip(p, BLUE)
    )


def read_pam(path):
    """RGB_ALPHA PAM as the viewer writes it (Pillow does not read P7)."""
    with open(path, "rb") as f:
        head = {}
        if f.readline().strip() != b"P7":
            raise ValueError(f"{path}: not a PAM")
        while True:
            line = f.readline().strip()
            if line == b"ENDHDR":
                break
            key, _, value = line.partition(b" ")
            head[key.decode()] = value.decode()
        w, h = int(head["WIDTH"]), int(head["HEIGHT"])
        if head.get("DEPTH") != "4" or head.get("MAXVAL") != "255":
            raise ValueError(f"{path}: expected 8-bit RGBA")
        return Image.frombytes("RGBA", (w, h), f.read(w * h * 4))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shot")
    ap.add_argument("rect")
    ap.add_argument("--min-hole", type=float, default=0.9)
    ap.add_argument("--min-pattern", type=float, default=0.5)
    a = ap.parse_args()
    x, y, w, h = (int(v) for v in a.rect.split(","))
    osd = read_pam(a.shot + ".osd.pam")
    ow, oh = osd.size
    alpha = osd.getchannel("A")
    step = 4
    inside = outside = hole = clear_out = 0
    for py in range(0, oh, step):
        for px in range(0, ow, step):
            a0 = alpha.getpixel((px, py)) == 0
            if x <= px < x + w and y <= py < y + h:
                inside += 1
                hole += a0
            else:
                outside += 1
                clear_out += a0
    shot = Image.open(a.shot).convert("RGB")
    sw, sh = shot.size
    sx, sy = sw / ow, sh / oh
    seen = pattern = 0
    for py in range(int(y * sy), int((y + h) * sy), 2):
        for px in range(int(x * sx), int((x + w) * sx), 2):
            if 0 <= px < sw and 0 <= py < sh:
                seen += 1
                pattern += is_pattern(shot.getpixel((px, py)))
    hole_share = hole / inside if inside else 0.0
    pattern_share = pattern / seen if seen else 0.0
    print(f"osd {ow}x{oh}, rect {x},{y} {w}x{h}")
    print(
        f"alpha 0 inside rect: {hole_share:.3f}; outside: "
        f"{(clear_out / outside if outside else float('nan')):.3f}"
    )
    samples = [
        (x + w // 2, y + h // 2),
        (x + w // 8, y + h // 8),
        (x + w - w // 8, y + h - h // 8),
        (x - 20, y + h // 2),
        (x + w + 20, y + h // 2),
    ]
    for px, py in samples:
        if 0 <= px < ow and 0 <= py < oh:
            where = "in " if x <= px < x + w and y <= py < y + h else "out"
            print(
                f"  {where} ({px},{py}): osd RGBA {osd.getpixel((px, py))}, "
                f"shown {shot.getpixel((min(int(px * sx), sw - 1), min(int(py * sy), sh - 1)))}"
            )
    print(f"checkerboard inside rect in the composed picture: {pattern_share:.3f}")
    ok = hole_share >= a.min_hole and pattern_share >= a.min_pattern
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
