#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile dtv_soc.c's parser of the host's frames (include/webos-tuner.h) on the host and feed it
what the host's tuner sends: a map, transport stream frames split at every byte boundary the
pump may see, an empty map, and garbage before and between frames, which it skips a byte at a
time. Packets come out whole, in order, with their frequency; the lock tolerance takes a tune
within 1 MHz of a mapped centre frequency with its delivery system and nothing else."""

import random
import struct
import subprocess
from pathlib import Path

import pytest

MODULE = Path(__file__).resolve().parents[3] / "guest/kernel/modules/dtv_soc"
INCLUDE = MODULE.parents[3] / "include"
MAGIC, MAP, TS = 0x4E555457, 1, 2

RUNNER = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "webos-tuner.h"
typedef uint8_t u8;
typedef uint32_t u32;
#define LOCK_TOLERANCE_HZ 1000000
#define min(a, b) ((a) < (b) ? (a) : (b))
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define min3(a, b, c) min(min(a, b), c)
@SECTION@
static void on_map(void *ctx, const struct webos_tuner_channel *f, unsigned int n)
{
    printf("map");
    for (unsigned i = 0; i < n; i++) printf(" %u/%u", f[i].freq_hz, f[i].delsys);
    printf("\n");
}
static void on_ts(void *ctx, u32 freq, const u8 *p, unsigned int n)
{
    for (unsigned i = 0; i < n; i++) printf("ts %u %02x%02x\n", freq, p[188 * i + 1], p[188 * i + 2]);
}
int main(int argc, char **argv)
{
    static u8 buf[1 << 20];
    static struct tuner_parser p;
    struct tuner_sink s = {on_map, on_ts, NULL};
    size_t n = fread(buf, 1, sizeof buf, stdin), at = 0;
    unsigned seed = (unsigned)atoi(argv[1]);
    while (at < n) { /* chunks of 1 to 400 bytes, as reads of the port come */
        size_t k = seed ? 1 + (seed = seed * 1103515245u + 12345u) % 400 : n;
        if (k > n - at) k = n - at;
        tuner_feed(&p, &s, buf + at, k);
        at += k;
    }
    printf("resyncs %lu frames %lu\n", p.resyncs, p.frames);
    struct webos_tuner_channel map[] = {{177000000, 11}, {473000000, 11}};
    printf("mapped %d %d %d %d %d %d\n", tuner_mapped(map, 2, 177000000, 11), tuner_mapped(map, 2, 177310000, 11),
           tuner_mapped(map, 2, 178000000, 11), tuner_mapped(map, 2, 183000000, 11), tuner_mapped(map, 2, 0, 11),
           tuner_mapped(map, 2, 177000000, 2));
    return 0;
}
"""


def frame(kind, freq, payload):
    return struct.pack("<IIII", MAGIC, kind, freq, len(payload)) + payload


def packet(pid):
    return bytes([0x47, pid >> 8, pid & 0xFF, 0x10]) + bytes(184)


@pytest.fixture(scope="module")
def runner(tmp_path_factory):
    source = (MODULE / "dtv_soc.c").read_text()
    start = source.index("/* ---- the host's frames ----")
    section = source[start : source.index("/* ---- end of the host's frames ---- */")]
    root = tmp_path_factory.mktemp("dtv_soc_frames")
    (root / "runner.c").write_text(
        RUNNER.replace("@SECTION@", section).replace(
            "#include <stdint.h>", "#include <stdint.h>\n#include <stdlib.h>"
        )
    )
    subprocess.run(
        [
            "cc",
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-function",
            "-Wno-unused-parameter",
            "-I",
            str(INCLUDE),
            "-o",
            str(root / "runner"),
            str(root / "runner.c"),
        ],
        check=True,
    )
    return root / "runner"


def run_stream(runner, data, seed):
    out = subprocess.run([str(runner), str(seed)], input=data, capture_output=True, check=True)
    return out.stdout.decode().splitlines()


def test_frames_any_split(runner):
    data = (
        b"\x01\x02junk"
        + frame(MAP, 0, struct.pack("<IIII", 177000000, 11, 473000000, 11))
        + frame(TS, 177000000, b"".join(packet(p) for p in (0, 0x1FFB, 0x31)))
        + b"WTU"  # a broken header start
        + frame(TS, 473000000, packet(0x1D00))
        + frame(MAP, 0, b"")
    )
    want = [
        "map 177000000/11 473000000/11",
        "ts 177000000 0000",
        "ts 177000000 1ffb",
        "ts 177000000 0031",
        "ts 473000000 1d00",
        "map",
    ]
    for seed in (0, 1, 7, 99, 12345):
        lines = run_stream(runner, data, seed)
        assert lines[: len(want)] == want, f"seed {seed}"
        assert lines[len(want)] == "resyncs 9 frames 4", f"seed {seed}"


def test_bad_lengths_skipped(runner):
    bad_ts = struct.pack("<IIII", MAGIC, TS, 1, 100)  # not whole packets
    bad_map = struct.pack("<IIII", MAGIC, MAP, 0, 8 * 129)  # longer than the map may be
    bad_type = struct.pack("<IIII", MAGIC, 9, 0, 0)
    data = bad_ts + bad_map + bad_type + frame(TS, 5, packet(0x10))
    lines = run_stream(runner, data, 3)
    assert lines[0] == "ts 5 0010"


def test_tolerance(runner):
    lines = run_stream(runner, b"", 0)
    assert lines[-1] == "mapped 1 1 1 0 0 0"


def test_large_random(runner):
    rng = random.Random(205)
    data, want = b"", []
    for i in range(200):
        pids = [rng.randrange(0x1FFF) for _ in range(rng.randrange(1, 40))]
        data += frame(TS, 1000 + i, b"".join(packet(p) for p in pids))
        want += [f"ts {1000 + i} {p:04x}" for p in pids]
    lines = run_stream(runner, data, 42)
    assert lines[: len(want)] == want
    assert lines[len(want)] == "resyncs 0 frames 200"
