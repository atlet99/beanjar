#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The manager window and the viewer without a display (beanjar_ui.no_display): one line and a
non-zero exit, not a traceback. Needs GTK 4 and libadwaita for Python; skipped without them."""

import os
import subprocess
import sys

import pytest

TOP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
PYTHONPATH = os.pathsep.join(os.path.join(TOP, "host", p) for p in ("ui", "core"))
HAS_GTK = (
    subprocess.run(
        [
            sys.executable,
            "-c",
            "import gi; gi.require_version('Gtk', '4.0'); gi.require_version('Adw', '1'); "
            "from gi.repository import Adw, Gtk",
        ],
        capture_output=True,
        check=False,
    ).returncode
    == 0
)


def run_without_display(tmp_path, code):
    env = {
        k: v
        for k, v in os.environ.items()
        if k not in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "GDK_BACKEND")
    }
    # an empty runtime directory: GTK would otherwise try its default wayland-0 socket there
    env.update(XDG_RUNTIME_DIR=str(tmp_path), PYTHONPATH=PYTHONPATH, HOME=str(tmp_path))
    return subprocess.run(
        [sys.executable, "-c", code], env=env, capture_output=True, text=True, timeout=60
    )


@pytest.mark.skipif(not HAS_GTK, reason="no GTK 4 and libadwaita for Python")
def test_manager_without_display(tmp_path):
    r = run_without_display(
        tmp_path,
        f"import sys; from beanjar_ui.manager.gui import main; "
        f"sys.exit(main(['--library', {str(tmp_path / 'lib')!r}]))",
    )
    assert r.returncode == 1
    assert "Traceback" not in r.stderr
    assert r.stderr.strip().splitlines()[-1] == (
        "beanjar: no display to open a window on; run it in a desktop session, "
        "or use the command line (beanjar --help)"
    )


@pytest.mark.skipif(not HAS_GTK, reason="no GTK 4 and libadwaita for Python")
def test_viewer_without_display(tmp_path):
    r = run_without_display(
        tmp_path,
        "from beanjar_ui.viewer.main import main; main(['unix:path=/nonexistent'])",
    )
    assert r.returncode == 1
    assert "Traceback" not in r.stderr
    assert r.stderr.strip() == (
        "viewer: no display to open a window on; run it in a desktop session, or use --headless"
    )
