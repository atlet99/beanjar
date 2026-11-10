#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check a recording of the guest's test tone.

    check-audio.py FILE.wav [options]           check the viewer's --audio-wav output
    check-audio.py FILE.raw --raw 44100:2:s16   check headerless PCM (SDL's disk audio driver)
    check-audio.py --make-tone OUT.wav [--freq 1000 --seconds 3 --level -12]
                                                write a test tone (for paplay in the guest)

Checks, each printed as PASS/FAIL, exit status 1 on any failure:
  header    the WAV's format is --expect-format (default 44100:2:s16, the fixed QEMU audiodev)
  peak      the FFT peak of the active part (channels summed) is --freq +- --freq-tol Hz
  rms       the RMS of the active part is above --min-rms dBFS
  duration  the active part (first to last non-silent sample) lasts --seconds +- 5 %
            (skipped without --seconds)
  gaps      no run of silence inside the active part is longer than --max-gap ms
"Silent" is |sample| <= --silence in every channel (default 0: digital zeros). --segments N
prints the RMS of N equal slices of the active part, e.g. to see a volume change. --bursts MS
lists the sounds separated by more than MS of silence (start, length, level, peak).
Standard library only (the workstation has no numpy).
"""

import argparse
import cmath
import math
import struct
import sys
import wave

FORMATS = {"s16": 2}


def parse_fmt(text):
    rate, ch, fmt = text.split(":")
    if fmt not in FORMATS:
        raise SystemExit(f"unsupported sample format {fmt} (only s16)")
    return int(rate), int(ch), fmt


def read_pcm(path, raw):
    """Returns (rate, channels, fmt, frames as list of tuples)."""
    if raw:
        rate, ch, fmt = parse_fmt(raw)
        data = open(path, "rb").read()
    else:
        with wave.open(path, "rb") as w:
            if w.getcomptype() != "NONE":
                raise SystemExit("compressed WAV")
            rate, ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
            fmt = {2: "s16"}.get(width)
            if fmt is None:
                return rate, ch, f"{8 * width}-bit", []
            data = w.readframes(w.getnframes())
    n = len(data) // (2 * ch)
    samples = struct.unpack(f"<{n * ch}h", data[: n * ch * 2])
    frames = [samples[i * ch : (i + 1) * ch] for i in range(n)]
    return rate, ch, fmt, frames


def fft(x):
    """Iterative radix-2 FFT, len(x) a power of two."""
    n = len(x)
    a = list(map(complex, x))
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    while size <= n:
        w = cmath.exp(-2j * math.pi / size)
        half = size // 2
        for start in range(0, n, size):
            wk = 1
            for k in range(half):
                u, v = a[start + k], a[start + k + half] * wk
                a[start + k], a[start + k + half] = u + v, u - v
                wk *= w
        size *= 2
    return a


def peak_hz(mono, rate):
    """Frequency of the strongest bin over a Hann-windowed power-of-two slice: the loudest of the
    slices at quarter-length steps, so pauses between several sounds do not matter."""
    n = 1
    while n * 2 <= min(len(mono), 65536):
        n *= 2
    if n < 1024:
        return None
    starts = range(0, len(mono) - n + 1, n // 4)
    start = max(starts, key=lambda s: sum(v * v for v in mono[s : s + n : 16]))
    x = [mono[start + i] * (0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1))) for i in range(n)]
    mag = [abs(c) for c in fft(x)[1 : n // 2]]
    k = max(range(len(mag)), key=mag.__getitem__)
    # Parabolic interpolation on log magnitudes of the neighbouring bins.
    off = 0.0
    if 0 < k < len(mag) - 1 and min(mag[k - 1 : k + 2]) > 0:
        l, c, r = (math.log(m) for m in mag[k - 1 : k + 2])
        den = l - 2 * c + r
        off = 0.5 * (l - r) / den if den else 0.0
    return (k + 1 + off) * rate / n


def dbfs(values):
    if not values:
        return -math.inf
    ms = sum(v * v for v in values) / len(values)
    return 10 * math.log10(ms) - 20 * math.log10(32768) if ms > 0 else -math.inf


def make_tone(path, freq, seconds, level, rate=44100):
    amp = 32767 * 10 ** (level / 20)
    n = int(seconds * rate)
    frames = bytearray()
    for i in range(n):
        s = int(round(amp * math.sin(2 * math.pi * freq * i / rate)))
        frames += struct.pack("<hh", s, s)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))
    print(f"wrote {path}: {freq} Hz, {seconds} s, {level} dBFS, {rate} Hz stereo s16")


def main():
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("file", nargs="?")
    p.add_argument("--raw", metavar="RATE:CH:FMT")
    p.add_argument("--expect-format", default="44100:2:s16")
    p.add_argument("--freq", type=float, default=1000)
    p.add_argument("--freq-tol", type=float, default=5)
    p.add_argument("--min-rms", type=float, default=-30, help="dBFS (default -30)")
    p.add_argument("--seconds", type=float)
    p.add_argument("--max-gap", type=float, default=20, help="ms (default 20)")
    p.add_argument("--silence", type=int, default=0)
    p.add_argument("--segments", type=int, default=0)
    p.add_argument("--bursts", type=float, default=0, metavar="MS")
    p.add_argument("--make-tone", metavar="OUT")
    p.add_argument("--level", type=float, default=-12, help="dBFS for --make-tone")
    a = p.parse_args()
    if a.make_tone:
        make_tone(a.make_tone, a.freq, a.seconds or 3, a.level)
        return 0
    if not a.file:
        p.error("no input file")

    rate, ch, fmt, frames = read_pcm(a.file, a.raw)
    results = []

    def check(name, ok, detail):
        results.append(ok)
        print(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")

    want = parse_fmt(a.expect_format)
    check(
        "header",
        (rate, ch, fmt) == want,
        f"{rate} Hz, {ch} ch, {fmt} (expected {want[0]} Hz, {want[1]} ch, {want[2]})",
    )
    if not frames or fmt != "s16":
        check("data", False, "no s16 frames")
        return 1
    total = len(frames) / rate
    loud = [i for i, f in enumerate(frames) if max(abs(s) for s in f) > a.silence]
    if not loud:
        check("data", False, f"{total:.2f} s, all silent")
        return 1
    first, last = loud[0], loud[-1]
    active = frames[first : last + 1]
    mono = [sum(f) for f in active]
    print(
        f"file: {total:.3f} s; active {first / rate:.3f}..{(last + 1) / rate:.3f} s "
        f"({len(active) / rate:.3f} s)"
    )

    hz = peak_hz(mono, rate)
    check(
        "peak",
        hz is not None and abs(hz - a.freq) <= a.freq_tol,
        f"{hz:.2f} Hz (expected {a.freq:g} +- {a.freq_tol:g})" if hz else "too short",
    )
    # Level of the loudest channel in each frame: a tone in one channel at a time (speaker-test)
    # and the same tone in both read the same.
    per_ch = [max(f, key=abs) for f in active]
    level = dbfs(per_ch)
    check("rms", level >= a.min_rms, f"{level:.1f} dBFS (min {a.min_rms:g})")
    if a.seconds:
        dur = len(active) / rate
        check(
            "duration",
            abs(dur - a.seconds) <= 0.05 * a.seconds,
            f"{dur:.3f} s (expected {a.seconds:g} +- 5 %)",
        )
    longest, run, at = 0, 0, 0
    for i, f in enumerate(active):
        if max(abs(s) for s in f) <= a.silence:
            run += 1
            if run > longest:
                longest, at = run, i - run + 1
        else:
            run = 0
    gap_ms = longest * 1000 / rate
    check(
        "gaps",
        gap_ms <= a.max_gap,
        f"longest silence {gap_ms:.1f} ms"
        + (f" at {(first + at) / rate:.3f} s" if longest else "")
        + f" (max {a.max_gap:g})",
    )
    if a.segments:
        n = len(active) // a.segments
        print(
            "segments (dBFS): "
            + " ".join(f"{dbfs(per_ch[i * n : (i + 1) * n]):.1f}" for i in range(a.segments))
        )
    if a.bursts:
        split = int(a.bursts * rate / 1000)
        bursts, start, quiet = [], None, 0
        for i, f in enumerate(frames):
            if max(abs(s) for s in f) > a.silence:
                if start is None:
                    start = i
                quiet = 0
            elif start is not None:
                quiet += 1
                if quiet > split:
                    bursts.append((start, i - quiet + 1))
                    start = None
        if start is not None:
            bursts.append((start, len(frames) - quiet))
        for n, (b, e) in enumerate(bursts):
            seg = frames[b:e]
            hz = peak_hz([sum(f) for f in seg], rate)
            print(
                f"burst {n}: {b / rate:.3f} s, {(e - b) / rate:.3f} s, "
                f"{dbfs([max(f, key=abs) for f in seg]):.2f} dBFS, peak {hz or 0:.1f} Hz"
            )
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
