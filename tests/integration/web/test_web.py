# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-web (host/web) end to end: against beanjar-vnc and the stand-in QEMU display with a
stand-in manager CLI (check-web.py), headless Chromium on the viewer page (check-viewer.py), and its
traffic inspector against the real pinned mitmweb (check-mitm.py). No VM; each check a program of
its own, skipped (77) without what it needs (python3-aiohttp, Debian's novnc or bootstrap-icons,
chromium, `beanjar mitm setup`'s mitmweb)."""

from pathlib import Path
import sys

import pytest

HERE = Path(__file__).resolve().parent


@pytest.mark.parametrize("name", ["check-web", "check-viewer"])
def test_web(name, artefact, run_check):
    run_check([sys.executable, HERE / f"{name}.py", artefact("vnc")], 90)


def test_mitm(run_check):
    run_check([sys.executable, HERE / "check-mitm.py"], 90)
