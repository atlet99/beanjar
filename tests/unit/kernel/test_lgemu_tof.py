#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the TMF8821's register model (tof.c) on the host and drive it as msc's direct TMF882X
driver does: power-up and CPU ready at 0xE0, the app ID and version, the configuration
page commands, a measurement with its interrupt at 0xE1 and the 52-byte result at 0x20, and
msc's motion rule over the results (tof8801 result parser, W22O 43.21.89 0x24274): a person who
is there and moves makes motion at each step, nobody or a still scene makes none."""

import subprocess
from pathlib import Path

MODULE = Path(__file__).resolve().parents[3] / "guest/kernel/modules/lgemu"


def test_registers(tmp_path):
    source = (MODULE / "tof.c").read_text()
    start = source.index("/* ---- the TMF8821's registers")
    section = source[start : source.index("/* ---- end of the TMF8821's registers ---- */")]
    root = tmp_path
    runner = root / "runner.c"
    runner.write_text(
        r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
"""
        + section
        + r"""
/* DILE_I2C_Read: the register written, then len bytes read after a repeated start */
static void rd(struct tof *c, u8 reg, u8 *buf, unsigned int len) { tof_write(c, &reg, 1); tof_read(c, buf, len); }
static u8 rd1(struct tof *c, u8 reg) { u8 b; rd(c, reg, &b, 1); return b; }
/* DILE_I2C_Write: the register and the data in one message */
static void wr1(struct tof *c, u8 reg, u8 v) { u8 b[2] = {reg, v}; tof_write(c, b, 2); }
static void wr16(struct tof *c, u8 reg, u16 v) { u8 b[3] = {reg, v & 0xff, v >> 8}; tof_write(c, b, 3); }
static void cmd(struct tof *c, u8 v) { wr1(c, 0x08, v); assert(rd1(c, 0x08) < 0x10); }

/* msc's motion rule (0x24274): per zone, a distance of 100 mm or more with enough confidence
 * that moves by more than 4 % (< 2000 mm) and then holds for `target` results is motion */
struct msc { u16 ref[9]; u8 count[9], moved[9]; u8 target; };
static int msc_frame(struct msc *m, const u8 *res)
{
    int motion = 0;
    for (int i = 0; i < 9; i++) {
        u8 conf = res[24 + 3 * i];
        u16 d = res[25 + 3 * i] | res[26 + 3 * i] << 8;
        unsigned tol;
        if (d < 100) continue;
        tol = d <= 2999 ? (unsigned)(d * 0.04) : (unsigned)(d * 0.02);
        if (conf < (d < 2000 ? 50 : d <= 3499 ? 15 : 10)) { m->count[i] = 0; m->moved[i] = 0; continue; }
        if (m->ref[i] && (unsigned)abs(d - m->ref[i]) > tol) {
            if (m->moved[i]) { m->count[i] = 0; m->ref[i] = d; }
            m->moved[i] = 1;
        } else {
            if (!m->ref[i]) m->ref[i] = d;
            m->count[i]++;
        }
        if (m->count[i] == m->target && m->moved[i]) { motion = 1; m->moved[i] = 0; }
    }
    return motion;
}
/* tof8801_app0_process_irq: 0xE1 bit 0, clear it, read the result */
static int poll(struct tof *c, u8 *res)
{
    u8 st = rd1(c, 0xE1);
    if (!(st & 1)) return 0;
    wr1(c, 0xE1, st);
    rd(c, 0x20, res, 52);
    return 1;
}
static int motions(struct tof *c, struct msc *m, int frames)
{
    u8 res[52];
    int n = 0;
    for (int f = 0; f < frames; f++) {
        assert(poll(c, res));
        assert(res[0] == 0x10);
        n += msc_frame(m, res);
    }
    return n;
}

int main(void)
{
    struct tof c = {0};
    u8 b[192];
    u16 mm;

    tof_reset(&c);
    /* tof8801_wait_for_cpu_ready: 0xE0 = 1, then 0x41 */
    wr1(&c, 0xE0, 0x00); assert(rd1(&c, 0xE0) == 0x00);
    wr1(&c, 0xE0, 0x01); assert(rd1(&c, 0xE0) == 0x41);
    /* ReadVersion: ID, revision, app (not the bootloader), SWVersion 20 */
    assert(rd1(&c, 0xE3) == 0x08);
    rd(&c, 0x00, b, 4); assert(b[0] == 0x02 && b[2] == 20);
    /* LoadConfig: load common, period, SPAD map, GPIO, iterations, write, clear interrupts */
    cmd(&c, 0x16);
    rd(&c, 0x20, b, 4); assert(b[0] == 0x16);
    wr16(&c, 0x24, 100); wr1(&c, 0x34, 11); wr1(&c, 0x31, 3); wr16(&c, 0x26, 0x0FF8);
    cmd(&c, 0x15);
    wr1(&c, 0xE1, 0xFF);
    /* the page keeps what was written to it */
    cmd(&c, 0x16);
    rd(&c, 0x20, b, 0x16); assert(b[0] == 0x16 && b[4] == 100 && b[0x14] == 11 && b[6] == 0xF8);
    /* StartFactoryCalibration: calibrate, the 192-byte page, load it, write it back */
    cmd(&c, 0x20); rd(&c, 0x20, b, 192);
    cmd(&c, 0x19); rd(&c, 0x20, b, 4); assert(b[0] == 0x19);
    cmd(&c, 0x15);
    /* no result before a measurement */
    assert(!(rd1(&c, 0xE1) & 1));
    /* start_capture: clear, measure, accepted */
    wr1(&c, 0xE1, 0xFF);
    wr1(&c, 0x08, 0x10); assert(rd1(&c, 0x08) == 0x01);

    /* nobody: no zone has an object, no motion */
    struct msc m = {.target = 4}; /* medium */
    { u8 res[52]; assert(poll(&c, res)); assert(res[6] == 0);
      for (int i = 0; i < 9; i++) assert(res[24 + 3 * i] == 0 && res[25 + 3 * i] == 0); }
    /* the interrupt stays until cleared: the result does not move on under it */
    { u8 r0 = rd1(&c, 0xE1), n0; rd(&c, 0x24, &n0, 1); assert(r0 & 1);
      assert(rd1(&c, 0xE1) & 1); u8 n1; rd(&c, 0x24, &n1, 1); assert(n0 == n1);
      wr1(&c, 0xE1, 0x01); }
    assert(motions(&c, &m, 50) == 0);

    /* a person at 1500 mm who moves: motion about once a step (10 results) */
    c.presence = 1500;
    int n = motions(&c, &m, 100);
    assert(n >= 8 && n <= 11);
    { u8 res[52]; assert(poll(&c, res)); assert(res[6] == 9 && res[24] == 200);
      u16 d = res[25] | res[26] << 8; assert(d == 1500 || d == 1687); }
    /* at every sensitivity, and far away */
    for (u8 t = 2; t <= 6; t += 2) { struct msc k = {.target = t}; assert(motions(&c, &k, 100) >= 8); }
    c.presence = 4000; { struct msc k = {.target = 4}; assert(motions(&c, &k, 100) >= 8); }
    /* gone again: no motion */
    c.presence = 0; assert(motions(&c, &m, 100) == 0);
    /* stop: no more results */
    cmd(&c, 0xFF); { u8 st = rd1(&c, 0xE1); wr1(&c, 0xE1, st); } assert(!(rd1(&c, 0xE1) & 1));

    /* the parameter */
    assert(!tof_parse("absent", &mm) && mm == 0);
    assert(!tof_parse("present\n", &mm) && mm == TOF_PRESENT_MM);
    assert(!tof_parse("0", &mm) && mm == 0);
    assert(!tof_parse("2500\n", &mm) && mm == 2500);
    assert(!tof_parse("100", &mm) && mm == 100);
    assert(!tof_parse("5000", &mm) && mm == 5000);
    assert(tof_parse("99", &mm) == -EINVAL);
    assert(tof_parse("5001", &mm) == -EINVAL);
    assert(tof_parse("-1", &mm) == -EINVAL);
    assert(tof_parse("1x", &mm) == -EINVAL);
    assert(tof_parse("", &mm) == -EINVAL);
    puts("ok");
    return 0;
}
"""
    )
    binary = root / "runner"
    subprocess.run(
        ["cc", "-std=gnu11", "-Wall", "-Werror", "-o", str(binary), str(runner)],
        check=True,
    )
    result = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "ok"
