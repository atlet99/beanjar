#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build and run the vendor window-config rule's check over a fake Mesa, without a device."""

import pathlib
import subprocess
import tempfile

import pytest


def test_window_formats():
    root = pathlib.Path(__file__).resolve().parents[3]
    if not pathlib.Path("/usr/include/EGL/egl.h").exists():
        pytest.skip("EGL development headers unavailable")
    with tempfile.TemporaryDirectory(prefix="beanjar-window-formats-") as directory:
        test = pathlib.Path(directory) / "check-window-formats"
        subprocess.run(
            [
                "cc",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-g",
                str(root / "tests/unit/gl/check-window-formats.c"),
                "-ldl",
                "-o",
                str(test),
            ],
            check=True,
        )
        result = subprocess.run([str(test)], capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stderr
        assert result.stdout.strip() == "ok"
