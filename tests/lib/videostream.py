# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-vnc's video stream (host/vnc/video.h) for the tests: the header, the units, their NAL
units, and the stream decoded with ffmpeg (the frames as RGB, and whatever ffmpeg complains of)."""

import shutil
import struct
import subprocess

HEADER = 32
UNIT_HEAD = 16


def header(data):
    """(width, height, fps, codec, bitrate, encoder) and the rest; ValueError if it is not one."""
    if len(data) < HEADER:
        raise ValueError(f"a header of {len(data)} bytes")
    magic, size, w, h, fps, codec, bitrate = struct.unpack("<4sIHHHHI", data[:20])
    if magic != b"BJVI" or size != HEADER:
        raise ValueError(f"header {data[:HEADER].hex()} is not BJVI's")
    encoder = data[20:HEADER].split(b"\0", 1)[0].decode()
    return (w, h, fps, codec, bitrate, encoder), data[HEADER:]


def units(body):
    """[(key, timestamp us, access unit)] in body, and the bytes of an incomplete last one."""
    out = []
    while len(body) >= UNIT_HEAD:
        n, flags, ts = struct.unpack("<IIQ", body[:UNIT_HEAD])
        if len(body) < UNIT_HEAD + n:
            break
        out.append((bool(flags & 1), ts, body[UNIT_HEAD : UNIT_HEAD + n]))
        body = body[UNIT_HEAD + n :]
    return out, body


def nal_types(au):
    """The NAL unit types in an Annex-B access unit."""
    types, i = [], 0
    while True:
        i = au.find(b"\0\0\1", i)
        if i < 0 or i + 3 >= len(au):
            return types
        types.append(au[i + 3] & 0x1F)
        i += 3


def codec_string(au):
    """avc1.PPCCLL from the access unit's SPS, as the web viewer makes it; None without one."""
    i = 0
    while True:
        i = au.find(b"\0\0\1", i)
        if i < 0 or i + 7 > len(au):
            return None
        if au[i + 3] & 0x1F == 7:
            return "avc1." + au[i + 4 : i + 7].hex().upper()
        i += 3


def decode(stream, w, h):
    """(frames as rgb24 bytes, ffmpeg's complaints) of an Annex-B stream; ffmpeg must be there."""
    r = subprocess.run(
        ["ffmpeg", "-hide_banner", "-v", "error", "-f", "h264", "-i", "-", "-fps_mode",
         "passthrough", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        input=stream, capture_output=True, timeout=60,
    )  # fmt: skip
    size = w * h * 3
    frames = [r.stdout[i : i + size] for i in range(0, len(r.stdout) - size + 1, size)]
    return frames, (r.stderr.decode(errors="replace").strip() if r.returncode or r.stderr else "")


def decode_yuv(stream, w, h):
    """As decode, the frames as the decoder's YUV 4:2:0 (yuv420p: Y, then U, then V), unconverted."""
    r = subprocess.run(
        ["ffmpeg", "-hide_banner", "-v", "error", "-f", "h264", "-i", "-", "-fps_mode",
         "passthrough", "-f", "rawvideo", "-pix_fmt", "yuv420p", "-"],
        input=stream, capture_output=True, timeout=60,
    )  # fmt: skip
    size = w * h * 3 // 2
    frames = [r.stdout[i : i + size] for i in range(0, len(r.stdout) - size + 1, size)]
    return frames, (r.stderr.decode(errors="replace").strip() if r.returncode or r.stderr else "")


def have_ffmpeg():
    return shutil.which("ffmpeg") is not None


def pixel(frame, w, x, y):
    o = (y * w + x) * 3
    return tuple(frame[o : o + 3])


def close_to(a, b, tolerance=8):
    return all(abs(p - q) <= tolerance for p, q in zip(a, b))
