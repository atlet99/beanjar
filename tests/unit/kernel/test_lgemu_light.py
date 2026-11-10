#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the ambient light sensor's register model (light.c) on the host and check it as
pqcontroller drives each type: the ID read at 0x09 that tells the types apart, the init
writes, the two channels at 0x05/0x04 (type 2) and 0x50/0x51 (type 1), and the light's parser."""

import subprocess
from pathlib import Path

MODULE = Path(__file__).resolve().parents[3] / "guest/kernel/modules/lgemu"


def test_registers(tmp_path):
    source = (MODULE / "light.c").read_text()
    start = source.index("/* ---- the light sensor's registers")
    section = source[start : source.index("/* ---- end of the light sensor's registers ---- */")]
    root = tmp_path
    runner = root / "runner.c"
    runner.write_text(
        r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
"""
        + section
        + r"""
/* DILE_I2C_Read: the subaddress written, then len bytes read after a repeated start */
static void rd(struct eye *c, u8 sub, u8 *buf, unsigned int len) { eye_write(c, &sub, 1); eye_read(c, buf, len); }
/* DILE_I2C_Write: the subaddress (if any) and the data in one message */
static void wr(struct eye *c, const u8 *bytes, unsigned int len) { eye_write(c, bytes, len); }
static unsigned word(struct eye *c, u8 sub) { u8 b[2]; rd(c, sub, b, 2); return b[0] | b[1] << 8; }
/* DIL_VPQ_SensorCheck: byte 0 of the ID read, 0x35 type 2, else type 1 */
static int type_of(struct eye *c) { u8 b[2]; rd(c, 0x09, b, 2); return b[0] == 0x35 ? 2 : 1; }
int main(void)
{
    struct eye c = {0};
    u16 ch[2];

    /* type 2 (LIGHT_SENSOR_2, digitalEye OTP_eyeSensor) */
    c.type = 2; c.ch[0] = 1234; c.ch[1] = 567;
    eye_reset(&c);
    assert(type_of(&c) == 2);
    { u8 w[3] = {0x00, 0x01, 0x81}; wr(&c, w, 3); }
    { u8 w[3] = {0x00, 0x00, 0x01}; wr(&c, w, 3); }
    { u8 w[3] = {0x00, 0x40, 0x21}; wr(&c, w, 3); }
    assert(word(&c, 0x00) == 0x2140); /* the last word written stays */
    assert(word(&c, 0x05) == 1234);  /* luminance */
    assert(word(&c, 0x04) == 567);   /* visibleLuminance */
    assert(type_of(&c) == 2);
    /* the host changes the light: the next reads follow */
    c.ch[0] = 0; c.ch[1] = 0;
    assert(word(&c, 0x05) == 0 && word(&c, 0x04) == 0);
    c.ch[0] = 0xFFFF; assert(word(&c, 0x05) == 0xFFFF);
    /* a 4-byte read runs on into the next register's word */
    { u8 b[4]; c.ch[0] = 0x1122; c.ch[1] = 0x3344; rd(&c, 0x04, b, 4);
      assert(b[0] == 0x44 && b[1] == 0x33 && b[2] == 0x22 && b[3] == 0x11); }
    /* type 2 has no data at type 1's registers */
    assert(word(&c, 0x50) == 0);

    /* type 1 (LIGHT_SENSOR_1, digitalEye eye_sensor): three writes without a subaddress */
    memset(&c, 0, sizeof(c));
    c.type = 1; c.ch[0] = 3000; c.ch[1] = 6000;
    eye_reset(&c);
    { u8 w[2] = {0x00, 0x01}; wr(&c, w, 2); }
    { u8 w[2] = {0x00, 0x41}; wr(&c, w, 2); }
    { u8 w[2] = {0x00, 0x6e}; wr(&c, w, 2); }
    assert(type_of(&c) == 1);
    assert(word(&c, 0x50) == 3000);
    assert(word(&c, 0x51) == 6000);
    assert(word(&c, 0x05) == 0);
    assert(word(&c, 0x00) == 0x6e);

    /* the parameter: "ch0,ch1" or one value for both, a newline allowed, 0 to 65535 */
    assert(!eye_parse("100,200", ch) && ch[0] == 100 && ch[1] == 200);
    assert(!eye_parse("100,200\n", ch) && ch[0] == 100 && ch[1] == 200);
    assert(!eye_parse("3000\n", ch) && ch[0] == 3000 && ch[1] == 3000);
    assert(!eye_parse("65535,0", ch) && ch[0] == 65535 && ch[1] == 0);
    assert(eye_parse("65536", ch) == -EINVAL);
    assert(eye_parse("1,2,3", ch) == -EINVAL);
    assert(eye_parse("1x", ch) == -EINVAL);
    assert(eye_parse("", ch) == -EINVAL);
    assert(eye_parse("-1", ch) == -EINVAL);
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
