#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the viewer core's ctypes binding from its C header.

Reads the wv_* declarations and WV_ABI from host/viewer/core/core.h and
writes host/ui/beanjar_ui/viewer/_core_abi.py (signatures + ABI), which
beanjar_ui/viewer/core.py imports. With --check, exits nonzero when the checked
in file differs instead of writing it.

Pointer rules mirror the hand-written binding this replaces: void * is _P,
(const) char * is _S, wv_callback is _CB, wv_audio_fn is _PCM, other T * is POINTER(T).
"""

import argparse
import pathlib
import re
import sys

TOP = pathlib.Path(__file__).resolve().parent.parent
HEADER = TOP / "core" / "core.h"
OUTPUT = TOP.parent / "ui" / "beanjar_ui" / "viewer" / "_core_abi.py"

BASE = {
    "void": None,
    "int": "_I",
    "unsigned": "_U",
    "long": "ctypes.c_long",
    "wv_callback": "_CB",
    "wv_audio_fn": "_PCM",
}

DECL = re.compile(
    r"^(?P<ret>[A-Za-z_][\w\s\*]*?)\s*(?P<name>wv_\w+)\s*\((?P<args>[^)]*)\)\s*;", re.MULTILINE
)


def ctype(text):
    text = " ".join(text.replace("const", "").split())
    if text.endswith("*"):
        base = text[:-1].strip()
        if base == "void":
            return "_P"
        if base == "char":
            return "_S"
        if base in ("struct wv", "wv"):
            return "_P"
        return f"ctypes.POINTER({BASE.get(base, base)})"
    if text in ("struct wv", "wv"):
        return "_P"
    return BASE[text]


def argtype(text):
    text = text.strip()
    if "*" in text:
        return ctype(text.rsplit("*", 1)[0] + "*")
    return ctype(text.rsplit(" ", 1)[0])


def parse(header):
    abi = re.search(r"#define\s+WV_ABI\s+(\d+)", header).group(1)
    sigs = {}
    for m in DECL.finditer(header):
        ret = m.group("ret").strip()
        args = [a.strip() for a in m.group("args").split(",")]
        if args == ["void"]:
            args = []
        sigs[m.group("name")] = (
            "None" if ret == "void" else ctype(ret),
            [argtype(a) for a in args],
        )
    return abi, sigs


def render(abi, sigs):
    lines = [
        '"""Generated from core/core.h by tools/gen-ctypes.py. Do not edit."""',
        "import ctypes",
        "",
        f"ABI = {abi}",
        "_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p)",
        "_PCM = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.POINTER(ctypes.c_short), ctypes.c_int)",
        "_P = ctypes.c_void_p",
        "_I = ctypes.c_int",
        "_U = ctypes.c_uint",
        "_S = ctypes.c_char_p",
        "",
        "",
        "_SIGNATURES = {",
    ]
    for name, (ret, args) in sigs.items():
        lines.append(f'    "{name}": ({ret}, [{", ".join(args)}]),')
    lines += ["}", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    abi, sigs = parse(HEADER.read_text())
    assert len(sigs) >= 20, f"only {len(sigs)} wv_* declarations parsed"
    want = render(abi, sigs)
    if a.check:
        have = OUTPUT.read_text() if OUTPUT.exists() else ""
        if have != want:
            print(f"{OUTPUT} is stale; regenerate with tools/gen-ctypes.py", file=sys.stderr)
            return 1
        print(f"{OUTPUT} is current ({len(sigs)} signatures, ABI {abi})")
        return 0
    OUTPUT.write_text(want)
    print(f"wrote {OUTPUT} ({len(sigs)} signatures, ABI {abi})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
