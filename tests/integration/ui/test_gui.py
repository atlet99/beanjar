# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The VM manager's GTK window (host/ui) against the fake launcher, QEMU, viewer and epk2extract
(tests/fakes), on a private Xvfb display with this build's layouts: check-gui.py drives it with
direct calls, check-gui-reactive.py changes the library from outside it. Programs of their own
(the system Python with PyGObject, under xvfb-run); skipped (77) without GTK 4 for Python.
`xvfb-run -a tests/integration/ui/check-gui.py --shots build/manager` keeps the screenshots."""

from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent

CHECKS = {"check-gui": "1920x1200x24", "check-gui-reactive": "1280x800x24"}


@pytest.mark.parametrize("name", CHECKS)
def test_gui(name, artefact, artefacts, run_check):
    xvfb = artefact("xvfb_run")
    env = {"GTK_A11Y": "none"}  # no at-spi bus left behind
    if artefacts.get("manager_gresource"):
        env["BEANJAR_GRESOURCE"] = artefacts["manager_gresource"]
    run_check(
        [
            xvfb,
            "-a",
            "-s",
            f"-screen 0 {CHECKS[name]}",
            artefact("gtk_python"),
            HERE / f"{name}.py",
        ],
        120,
        env=env,
    )
