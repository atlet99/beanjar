#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The boot splash's picture (guest/initramfs/src/splash.c): the logo with the name to its right,
centred on the splash's background, laid out for a 1080-line screen, as a binary PPM. The layout
follows LG's own bootloader logo (the mark left of the wordmark, on #212121).

    splash.py --icon host/ui/data/icon.svg --rsvg-convert PATH --out splash.ppm

rsvg-convert (librsvg2-bin) renders one SVG made here: the icon, embedded as a data URI, and
"beanjar" in DejaVu Sans (fonts-dejavu-core). Its PNG becomes the PPM with the standard library
alone. The image's top-left pixel is the background, which splash.c fills the screen with.
"""

import argparse
import base64
import struct
import subprocess
import sys
import zlib

BACKGROUND = "#212121"  # LG's bootloader logo background
TEXT = "#ffffff"
WIDTH, HEIGHT = 856, 300  # the picture; the screen around it is the background
LOGO = 300  # the icon's size, at the left; its jar fills about 60..240 across
TEXT_X, BASELINE = 282, 192  # the name, to the right of the jar, centred on it
FONT_SIZE = 120


def svg(icon: bytes) -> str:
    href = "data:image/svg+xml;base64," + base64.b64encode(icon).decode()
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">
  <rect width="{WIDTH}" height="{HEIGHT}" fill="{BACKGROUND}"/>
  <image x="0" y="0" width="{LOGO}" height="{LOGO}" href="{href}"/>
  <text x="{TEXT_X}" y="{BASELINE}" fill="{TEXT}"
        font-family="DejaVu Sans" font-weight="bold" font-size="{FONT_SIZE}">beanjar</text>
</svg>
"""


def png_to_rgb(png: bytes):
    """(width, height, RGB bytes) of an 8-bit RGB or RGBA, non-interlaced PNG (rsvg-convert's)."""
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, ihdr = 8, b"", None
    while pos < len(png):
        n, kind = struct.unpack(">I4s", png[pos : pos + 8])
        data = png[pos + 8 : pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", data)
        elif kind == b"IDAT":
            idat += data
        elif kind == b"IEND":
            break
    w, h, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError(
            f"unsupported PNG: depth {depth}, colour type {ctype}, interlace {interlace}"
        )
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    out = bytearray()
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif f:
                raise ValueError(f"bad PNG filter {f}")
        prev = line
        if bpp == 3:
            out += line
        else:
            for i in range(0, stride, 4):
                out += line[i : i + 3]
    return w, h, bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--icon", required=True)
    ap.add_argument("--rsvg-convert", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    with open(a.icon, "rb") as f:
        icon = f.read()
    png = subprocess.run(
        [a.rsvg_convert, "--format", "png", "--background-color", BACKGROUND],
        input=svg(icon).encode(),
        stdout=subprocess.PIPE,
        check=True,
    ).stdout
    w, h, rgb = png_to_rgb(png)
    if (w, h) != (WIDTH, HEIGHT):
        sys.exit(f"splash.py: rsvg-convert made {w}x{h}, not {WIDTH}x{HEIGHT}")
    with open(a.out, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h) + rgb)


if __name__ == "__main__":
    main()
