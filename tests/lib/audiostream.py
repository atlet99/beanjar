# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-vnc's audio stream (host/vnc/audio.h) for the tests: the header, Opus packets decoded
with libopus through ctypes, and a check that the PCM is fake-qemu's tone (tests/fakes/fake-qemu.py
--audio: 1 kHz, -12 dBFS peak, so about -15 dBFS RMS, in both channels)."""

import ctypes
import ctypes.util
import math
import struct

RATE = 48000
FORMATS = {"pcm": 1, "opus": 2}
TONE_HZ = 1000
TONE_RMS_DB = 20 * math.log10(0.25 / math.sqrt(2))  # -15.05


def header(data, fmt):
    """The stream's 16-byte header; raises ValueError unless it is fmt's."""
    if len(data) < 16:
        raise ValueError(f"a header of {len(data)} bytes")
    magic, size, rate, channels, code = struct.unpack("<4sIIHH", data[:16])
    if (magic, size, rate, channels, code) != (b"BJAU", 16, RATE, 2, FORMATS[fmt]):
        raise ValueError(f"header {data[:16].hex()} is not {fmt}'s")
    return data[16:]


class Opus:
    """libopus's decoder (libopus0), 48 kHz stereo."""

    def __init__(self):
        name = ctypes.util.find_library("opus") or "libopus.so.0"
        self.lib = ctypes.CDLL(name)
        self.lib.opus_decoder_create.restype = ctypes.c_void_p
        self.lib.opus_decode.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32,
                                         ctypes.POINTER(ctypes.c_short), ctypes.c_int, ctypes.c_int]  # fmt: skip
        err = ctypes.c_int()
        self.dec = self.lib.opus_decoder_create(RATE, 2, ctypes.byref(err))
        if not self.dec:
            raise RuntimeError(f"opus_decoder_create: {err.value}")
        self.out = (ctypes.c_short * (5760 * 2))()

    def decode(self, packet):
        n = self.lib.opus_decode(self.dec, packet, len(packet), self.out, 5760, 0)
        if n < 0:
            raise ValueError(f"opus_decode: {n}")
        return bytes(self.out)[: n * 4]

    def close(self):
        self.lib.opus_decoder_destroy(ctypes.c_void_p(self.dec))


def packets(body):
    """The length-prefixed packets in body, and the bytes of an incomplete last one."""
    out = []
    while len(body) >= 2:
        n = body[0] | body[1] << 8
        if len(body) < 2 + n:
            break
        out.append(body[2 : 2 + n])
        body = body[2 + n :]
    return out, body


def to_pcm(body, fmt):
    """s16le stereo PCM from a stream's body (after the header)."""
    if fmt == "pcm":
        return body[: len(body) // 4 * 4]
    pkts, _rest = packets(body)
    dec = Opus()
    try:
        pcm = b"".join(dec.decode(p) for p in pkts)
    finally:
        dec.close()
    return pcm


def analyse(pcm):
    """(seconds, RMS dBFS of each channel, frequency by zero crossings, left == right) of the
    second half of pcm (past any start-up)."""
    n = len(pcm) // 4
    samples = struct.unpack(f"<{n * 2}h", pcm[: n * 4])
    left, right = samples[0::2][n // 2 :], samples[1::2][n // 2 :]

    def rms_db(x):
        r = math.sqrt(sum(v * v for v in x) / max(1, len(x))) / 32768
        return 20 * math.log10(r) if r > 0 else -math.inf

    # the median period between upward zero crossings: a gap in the stream (a block dropped for a
    # client that fell behind, by design) breaks one period, which a count over the whole span
    # would take as an extra or a missing cycle
    ups = [i for i, (a, b) in enumerate(zip(left, left[1:])) if a < 0 <= b]
    periods = sorted(b - a for a, b in zip(ups, ups[1:]))
    freq = RATE / periods[len(periods) // 2] if periods else 0
    same = rms_db([a - b for a, b in zip(left, right)]) < -40  # Opus may differ a little
    return n / RATE, rms_db(left), rms_db(right), freq, same


def check_tone(pcm, what, min_seconds=0.5):
    """Raises ValueError unless pcm holds min_seconds of the tone; returns a summary line."""
    secs, l_db, r_db, freq, same = analyse(pcm)
    if secs < min_seconds:
        raise ValueError(f"{what}: only {secs:.2f} s")
    if abs(freq - TONE_HZ) > 10:
        raise ValueError(f"{what}: {freq:.1f} Hz, not {TONE_HZ}")
    for db in (l_db, r_db):
        if abs(db - TONE_RMS_DB) > 1.5:
            raise ValueError(f"{what}: {db:.1f} dBFS RMS, not {TONE_RMS_DB:.1f}")
    if not same:
        raise ValueError(f"{what}: the channels differ")
    return f"{what}: {secs:.2f} s, {freq:.0f} Hz, {l_db:.1f} dBFS RMS"


def check_lowdelay(body):
    """Check the actual wire packets: 10 ms CELT-only (restricted low delay)."""
    pkts, _ = packets(body)
    dec = Opus()
    try:
        sizes = {len(dec.decode(p)) // 4 for p in pkts}
    finally:
        dec.close()
    # RFC 6716 section 3.1: TOC configurations 16..31 select CELT-only;
    # configurations 0..15 use SILK/hybrid with longer algorithmic delay.
    if not pkts or sizes != {480} or any(p[0] >> 3 < 16 for p in pkts):
        raise ValueError(
            f"Opus low delay: {len(pkts)} packets, frame sizes {sizes}, not 480-frame CELT"
        )
    return f"Opus low delay: {len(pkts)} CELT packets, 480 frames / 10 ms each"
