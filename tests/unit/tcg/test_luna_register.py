#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the benchmark against both observed LS2 registration contracts."""

import pathlib
import shutil
import subprocess

import pytest

HERE = (
    pathlib.Path(__file__).resolve().parents[3] / "tests/regress/tcg"
)  # the benchmark and ls2-compat.h
MOCK = r"""
#include <stdio.h>
#include "ls2-compat.h"
#ifdef LEGACY
bool LSRegisterPubPriv(const char *n, LSHandle **h, bool pub, LSError *e) {
    (void)n; (void)e; puts(pub ? "legacy public" : "legacy private");
    *h=(LSHandle *)1; return !pub;
}
#endif
#ifndef MISSING
bool LSRegister(const char *n, LSHandle **h, LSError *e) {
    (void)n; (void)e; puts("unified"); *h=(LSHandle *)1; return true;
}
#endif
bool LSErrorInit(LSError *e) { (void)e; return true; }
void LSErrorFree(LSError *e) { (void)e; }
void LSErrorPrint(LSError *e, void *f) { (void)e; (void)f; }
GMainLoop *g_main_loop_new(GMainContext *c, int r) { (void)c;(void)r;return (GMainLoop *)1; }
void g_main_loop_run(GMainLoop *l) { (void)l; }
void g_main_loop_quit(GMainLoop *l) { (void)l; }
unsigned g_timeout_add_seconds(unsigned n,int(*f)(void *),void *p) { (void)n;(void)f;(void)p;return 1; }
bool LSGmainAttach(LSHandle *h,GMainLoop *l,LSError *e) { (void)h;(void)l;(void)e;return true; }
const char *LSMessageGetPayload(LSMessage *m) { (void)m;return "{\"state\":\"Active\"}"; }
bool LSCallOneReply(LSHandle *h,const char *u,const char *p,LSMethodFunction f,void *c,unsigned long *t,LSError *e) {
    (void)u;(void)p;(void)t;(void)e;return f(h,(LSMessage *)1,c);
}
"""


@pytest.mark.parametrize(
    ("mode", "define", "expected"),
    [
        ("legacy", "LEGACY", "legacy private"),
        ("unified", None, "unified"),
        ("missing", "MISSING", None),
    ],
)
def test_observed_apis(tmp_path, mode, define, expected):
    cc = shutil.which("cc")
    if not cc:
        pytest.skip("native C compiler unavailable")
    mock = tmp_path / "mock.c"
    mock.write_text(MOCK)
    exe = tmp_path / mode
    subprocess.run(
        [
            cc,
            "-O2",
            "-rdynamic",
            "-I",
            str(HERE),
            *(["-D" + define] if define else []),
            str(HERE / "luna-perf.c"),
            str(mock),
            "-ldl",
            "-o",
            str(exe),
        ],
        check=True,
        capture_output=True,
    )
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == (0 if expected else 1)
    if expected:
        assert result.stdout.splitlines()[0] == expected
        assert "persistent1000 seconds=" in result.stdout
    else:
        assert "LS2 has no registration API" in result.stderr
