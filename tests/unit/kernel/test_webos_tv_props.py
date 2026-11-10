#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile webos_tv.c's LG private properties (lgfe_*) on the host and put them the questions both
tuner libraries ask: webOS 6's numbered properties (W21P, LG's dvbfrontend_ext.h: GET 0x81
the fe_status, FE_HAS_LOCK while locked; 0x5d the tuned frequency in kHz; 0x55 the special data,
valid 8VSB on a locked ATSC channel, after 0x56 stored one; 0x71 the monitor's two bytes; 0x60/0x61
strength and quality; 0x80 the post job's bytes from answers=; 0x68 the LIB version) and webOS 22's named
ones (an EXT_* name in the union, cmd garbage, the answer in the property's first word), the SETs
the driver takes from the DVB core, webOS 4's post job, and answers= parsing."""

import subprocess
from pathlib import Path

MODULE = Path(__file__).resolve().parents[3] / "guest/kernel/modules/webos_tv"

RUNNER = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define DTV_MAX_COMMAND 70
static int hex_to_bin(unsigned char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
@SECTION@
static u32 w(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
/* a 72-byte compat_dtv_property: cmd, then the union at 16 holding name (when given) */
static void prop(u8 *p, u32 cmd, const char *name)
{
    memset(p, 0xa5, 72); /* garbage, as a caller's stack */
    p[0] = cmd; p[1] = cmd >> 8; p[2] = cmd >> 16; p[3] = cmd >> 24;
    if (name) { memset(p + 16, 0, 32); strcpy((char *)p + 16, name); }
}
int main(void)
{
    u8 p[72];
    struct lgfe_cmds w21p = {.version = 0x68, .freq = 0x5d, .status = 0x81, .monitor = 0x71,
                             .specstore = 0x56, .speccheck = 0x55, .strength = 0x60,
                             .quality = 0x61, .version_text = "2.1.0"};
    struct lgfe_state on = {.locked = true, .freq = 177000000, .delsys = 11},
                      off = {.locked = false, .freq = 57000000, .delsys = 11};
    assert(!lgfe_parse_answers(&w21p, "0x80:6401"));
    assert(w21p.nanswers == 1 && w21p.answer[0].cmd == 0x80 && w21p.answer[0].len == 2);

    /* webOS 6, numbered (dvbfrontend_ext.h): GET_LOCK_STATUS, the demodulator's fe_status */
    prop(p, 0x81, NULL); assert(lgfe_get(&w21p, &on, p) == 1); assert(w(p + 16) == 0x10); assert(w(p) == 0x81);
    assert(((w(p + 16) ^ 0x10) >> 4 & 1) == 0); /* DIL_TU_CheckLockState: not failed */
    prop(p, 0x81, NULL); assert(lgfe_get(&w21p, &off, p) == 1); assert(w(p + 16) == 0);
    /* GET_TUNED_FREQUENCY, kHz */
    prop(p, 0x5d, NULL); lgfe_get(&w21p, &on, p); assert(w(p + 16) == 177000);
    struct lgfe_state sat = {.locked = true, .freq = 11000000, .delsys = 6};
    prop(p, 0x5d, NULL); lgfe_get(&w21p, &sat, p); assert(w(p + 16) == 11000000);
    /* CHECK_SPECAILDATA: valid 8VSB on a locked ATSC channel, the stored word otherwise */
    struct lgfe_state st = off;
    prop(p, 0x56, NULL); memset(p + 16, 0, 32); p[16] = 0x05; assert(lgfe_set(&w21p, &st, p) == 1);
    assert(st.specdata == 5);
    prop(p, 0x55, NULL); lgfe_get(&w21p, &on, p); assert(w(p + 16) == 0x50000020);
    assert((p[16 + 3] & 0xf0) == 0x50 && (p[16] & 0x1f) == 0); /* the VSB validator, VSB */
    prop(p, 0x55, NULL); lgfe_get(&w21p, &st, p); assert(w(p + 16) == 5);
    struct lgfe_state t2 = {.locked = true, .delsys = 16, .specdata = 2};
    prop(p, 0x55, NULL); lgfe_get(&w21p, &t2, p); assert(w(p + 16) == 0x22);
    /* MONITOR: two bytes, 0 and the state */
    prop(p, 0x71, NULL); lgfe_get(&w21p, &on, p); assert(p[16] == 0 && p[17] == 0x10 && w(p + 48) == 2);
    prop(p, 0x71, NULL); lgfe_get(&w21p, &off, p); assert(p[17] == 0);
    prop(p, 0x60, NULL); lgfe_get(&w21p, &on, p); assert(w(p + 16) == 80);
    prop(p, 0x61, NULL); lgfe_get(&w21p, &on, p); assert(w(p + 16) == 90);
    prop(p, 0x60, NULL); lgfe_get(&w21p, &off, p); assert(w(p + 16) == 0);
    prop(p, 0x80, NULL); lgfe_get(&w21p, &off, p); assert(p[16] == 0x64 && p[17] == 1 && p[18] == 0);
    prop(p, 0x68, NULL); lgfe_get(&w21p, &off, p);
    assert(!strcmp((char *)p + 16, "2.1.0") && w(p + 48) == 5);
    prop(p, 0x90, NULL); assert(lgfe_get(&w21p, &on, p) == 1); /* LG's, unanswered: zeros */
    for (int i = 4; i < 64; i++) assert(p[i] == 0);
    /* the standard ones are the core's */
    prop(p, 3, NULL); assert(lgfe_get(&w21p, &on, p) == 0); assert(p[16] == 0xa5);
    prop(p, 0x11, NULL); assert(lgfe_get(&w21p, &on, p) == 0);
    prop(p, 0x2c, NULL); assert(lgfe_get(&w21p, &on, p) == 0);

    /* webOS 22, named: cmd garbage, the answer at offset 0, the name left in place */
    prop(p, 0xa5a5a5a5u, "EXT_GET_LOCK_STATUS"); assert(lgfe_get(&w21p, &on, p) == 1);
    assert(w(p) == 0x10); assert(!strcmp((char *)p + 16, "EXT_GET_LOCK_STATUS"));
    prop(p, 0x11, "EXT_GET_LOCK_STATUS"); lgfe_get(&w21p, &off, p); assert(w(p) == 0);
    prop(p, 0x81, "EXT_GET_LOCK_STATUS"); lgfe_get(&w21p, &on, p); assert(w(p) == 0x10);
    prop(p, 0, "EXT_GET_TUNED_FREQUENCY"); lgfe_get(&w21p, &on, p); assert(w(p) == 177000);
    prop(p, 0, "EXT_CHECK_SPECAILDATA"); assert(lgfe_get(&w21p, &on, p) == 1); assert(w(p) == 0x50000020);
    /* a named SET's value is in the first word (W22O: 0x50000060 with EXT_STORE_SPECAILDATA) */
    st = off;
    prop(p, 0x50000060, "EXT_STORE_SPECAILDATA"); assert(lgfe_set(&w21p, &st, p) == 1);
    assert(st.specdata == 0x50000060);
    /* not a name: lower case, or no NUL within the buffer */
    prop(p, 3, "EXT_get"); assert(lgfe_get(&w21p, &on, p) == 0);
    prop(p, 3, NULL); memcpy(p + 16, "EXT_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 32); assert(!lgfe_name(p));

    /* every name the firmwares use is one */
    for (unsigned i = 0; i < sizeof(lgfe_names) / sizeof(lgfe_names[0]); i++) {
        prop(p, 0, lgfe_names[i]);
        assert(lgfe_name(p) && lgfe_set(&w21p, &st, p) == 1);
    }

    /* SETs: LG's (named, 0, above DTV_MAX_COMMAND) taken; the standard ones the core's */
    prop(p, 3, "EXT_SET_PLL"); assert(lgfe_set(&w21p, &st, p) == 1);
    prop(p, 0, NULL); memset(p + 16, 0, 32); assert(lgfe_set(&w21p, &st, p) == 1);
    prop(p, 0x7d, NULL); assert(lgfe_set(&w21p, &st, p) == 1);
    prop(p, 3, NULL); p[16] = 0x10; assert(lgfe_set(&w21p, &st, p) == 0);
    prop(p, 1, NULL); p[16] = 0; assert(lgfe_set(&w21p, &st, p) == 0); /* DTV_TUNE */
    prop(p, 0x11, NULL); p[16] = 11; assert(lgfe_set(&w21p, &st, p) == 0);

    /* webOS 4 (W19P): the HAL lock state and the post job's four words */
    struct lgfe_cmds w19p = {.version = 0xc1, .lock = 0xaf, .postjob = 0xb1, .version_text = "1.0.15"};
    prop(p, 0xaf, NULL); lgfe_get(&w19p, &off, p); assert(w(p + 16) == 1);
    prop(p, 0xb1, NULL); lgfe_get(&w19p, &off, p);
    assert(w(p + 16) == 1 && w(p + 20) == 100 && w(p + 24) == 0 && w(p + 28) == 1);
    prop(p, 0xb1, NULL); lgfe_get(&w19p, &on, p); assert(w(p + 16) == 0 && w(p + 28) == 1);

    /* answers= */
    struct lgfe_cmds a = {0};
    assert(!lgfe_parse_answers(&a, "0x80:6401,0x5d:01000000,93:ff"));
    assert(a.nanswers == 3 && a.answer[1].cmd == 0x5d && a.answer[1].len == 4 && a.answer[2].cmd == 93);
    struct lgfe_cmds b = {0};
    assert(lgfe_parse_answers(&b, "0x80:") < 0);
    struct lgfe_cmds c = {0};
    assert(lgfe_parse_answers(&c, "0x80:6401;") < 0);
    struct lgfe_cmds d = {0};
    char many[512] = "";
    for (int i = 0; i < 17; i++) sprintf(many + strlen(many), "%s%d:00", i ? "," : "", 0x90 + i);
    assert(lgfe_parse_answers(&d, many) < 0);
    puts("ok");
    return 0;
}
"""


def test_props(tmp_path):
    source = (MODULE / "webos_tv.c").read_text()
    start = source.index("/* ---- LG's private properties ----")
    section = source[start : source.index("/* ---- end of LG's private properties ---- */")]
    root = tmp_path
    (root / "runner.c").write_text(RUNNER.replace("@SECTION@", section))
    subprocess.run(
        [
            "cc",
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-function",
            "-o",
            str(root / "runner"),
            str(root / "runner.c"),
        ],
        check=True,
    )
    out = subprocess.run([str(root / "runner")], capture_output=True, text=True)
    assert out.returncode == 0, out.stderr
    assert out.stdout.strip() == "ok"
