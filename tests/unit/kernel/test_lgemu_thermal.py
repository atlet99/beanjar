#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile /proc/thermalinfo's value parsers (thermal.c) on the host and check them, and that a
file's text is what fancontroller's getSoCCurrentTemperature takes: atoi of up to 32 bytes."""

import subprocess
from pathlib import Path

MODULE = Path(__file__).resolve().parents[3] / "guest/kernel/modules/lgemu"


def test_values(tmp_path):
    source = (MODULE / "thermal.c").read_text()
    start = source.index("/* ---- the values")
    section = source[start : source.index("/* ---- end of the values ---- */")]
    root = tmp_path
    runner = root / "runner.c"
    runner.write_text(
        r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
"""
        + section
        + r"""
/* what the proc file reads as (soc_temperature_show, iddq_show), and fancontroller's view of it */
static int fancontroller(int value)
{
    char buf[33] = {0};
    snprintf(buf, 32, "%d\n", value);
    return atoi(buf);
}
int main(void)
{
    int v = 0, iddq[2];

    assert(!thermal_parse_int("45", THERMAL_MIN, THERMAL_MAX, &v) && v == 45);
    assert(!thermal_parse_int("85\n", THERMAL_MIN, THERMAL_MAX, &v) && v == 85);
    assert(!thermal_parse_int("-40", THERMAL_MIN, THERMAL_MAX, &v) && v == -40);
    assert(!thermal_parse_int("200", THERMAL_MIN, THERMAL_MAX, &v) && v == 200);
    assert(thermal_parse_int("201", THERMAL_MIN, THERMAL_MAX, &v) == -EINVAL);
    assert(thermal_parse_int("-41", THERMAL_MIN, THERMAL_MAX, &v) == -EINVAL);
    assert(thermal_parse_int("45C", THERMAL_MIN, THERMAL_MAX, &v) == -EINVAL);
    assert(thermal_parse_int("", THERMAL_MIN, THERMAL_MAX, &v) == -EINVAL);
    assert(!thermal_parse_iddq("150,90", iddq) && iddq[0] == 150 && iddq[1] == 90);
    assert(!thermal_parse_iddq("0,65534\n", iddq) && iddq[1] == 65534);
    assert(thermal_parse_iddq("150", iddq) == -EINVAL);
    assert(thermal_parse_iddq("150,65535", iddq) == -EINVAL);
    assert(thermal_parse_iddq("-1,2", iddq) == -EINVAL);
    assert(fancontroller(45) == 45 && fancontroller(-5) == -5 && fancontroller(150) == 150);
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
