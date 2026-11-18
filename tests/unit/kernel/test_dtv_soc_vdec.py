#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile dtv_soc's video decoder's reading of MPEG-2 headers (vdec.c) on the host and feed it
an elementary stream cut into packet payloads at every offset: a sequence header gives a FRAME
event of type 0, the first picture after it the PICINFO (once; again only when the format
changes), each picture header a FRAME event of its type, whether or not a start code is split over
two payloads; the picture info is the sequence header's size, frame rate and bit rate, progressive
as its extension says."""

import subprocess
from pathlib import Path

import pytest

MODULE = Path(__file__).resolve().parents[3] / "guest/kernel/modules/dtv_soc"

RUNNER = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define EV_PICINFO 0x8002
#define CARRY 15
struct picinfo {
    u32 h, v, aspect, bitrate, num, den;
    bool progressive;
};
struct vdec_file {
    u8 carry[CARRY];
    unsigned int carried;
    bool seq_seen, have_info;
    struct picinfo info, sent;
    u32 pictures, sequences, errors;
};
static void event(struct vdec_file *f, u32 id, const u8 *d, size_t n)
{
    printf("picinfo %ux%u %u/%u aspect %u bitrate %u progressive %d\n", f->sent.h, f->sent.v,
           f->sent.num, f->sent.den, f->sent.aspect, f->sent.bitrate, f->sent.progressive);
}
static void frame_event(struct vdec_file *f, u8 type)
{
    printf("frame %u\n", type);
}
@SECTION@
int main(int argc, char **argv)
{
    static u8 buf[1 << 16];
    static struct vdec_file f;
    size_t n = fread(buf, 1, sizeof buf, stdin), cut = (size_t)atoi(argv[1]);
    for (size_t at = 0; at < n; at += cut)
        scan(&f, buf + at, (unsigned)min(cut, n - at));
    printf("sequences %u pictures %u errors %u\n", f.sequences, f.pictures, f.errors);
    return 0;
}
"""


def sequence(w, h, aspect, rate, bitrate_400):
    return bytes(
        [
            0,
            0,
            1,
            0xB3,
            w >> 4,
            (w & 0xF) << 4 | h >> 8,
            h & 0xFF,
            aspect << 4 | rate,
            bitrate_400 >> 10,
            bitrate_400 >> 2 & 0xFF,
            (bitrate_400 & 3) << 6 | 0x20,
            0,
        ]
    )


def extension(progressive):
    return bytes([0, 0, 1, 0xB5, 0x14, 0x82 | progressive << 3, 0x01, 0x00, 0x01, 0x00])


def picture(kind):
    return bytes([0, 0, 1, 0x00, 0x00, kind << 3, 0xFF, 0xF8]) + bytes(40)


@pytest.fixture(scope="module")
def runner(tmp_path_factory):
    source = (MODULE / "vdec.c").read_text()
    start = source.index("/* ---- the stream's headers ----")
    section = source[start : source.index("/* ---- end of the stream's headers ---- */")]
    root = tmp_path_factory.mktemp("dtv_soc_vdec")
    (root / "runner.c").write_text(RUNNER.replace("@SECTION@", section))
    subprocess.run(
        [
            "cc",
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-function",
            "-Wno-unused-parameter",
            "-o",
            str(root / "runner"),
            str(root / "runner.c"),
        ],
        check=True,
    )
    return root / "runner"


def run(runner, data, cut):
    out = subprocess.run([str(runner), str(cut)], input=data, capture_output=True, check=True)
    return out.stdout.decode().splitlines()


def test_headers_any_cut(runner):
    gop = sequence(704, 480, 2, 4, 3750) + extension(1) + picture(1) + picture(3) + picture(2)
    data = gop + gop + sequence(1920, 1080, 3, 4, 3750) + extension(0) + picture(1)
    want = [
        "frame 0",
        "picinfo 704x480 30000/1001 aspect 2 bitrate 1500 progressive 1",
        "frame 1",
        "frame 3",
        "frame 2",
        "frame 0",
        "frame 1",
        "frame 3",
        "frame 2",
        "frame 0",
        "picinfo 1920x1080 30000/1001 aspect 3 bitrate 1500 progressive 0",
        "frame 1",
        "sequences 3 pictures 7 errors 0",
    ]
    for cut in (len(data), 184, 100, 13, 7, 5, 4, 3, 2, 1):
        assert run(runner, data, cut) == want, cut


def test_no_picture_before_a_sequence(runner):
    data = picture(1) + picture(2) + sequence(704, 480, 2, 4, 3750) + picture(1)
    assert run(runner, data, 184) == [
        "frame 0",
        "picinfo 704x480 30000/1001 aspect 2 bitrate 1500 progressive 1",
        "frame 1",
        "sequences 1 pictures 1 errors 0",
    ]
