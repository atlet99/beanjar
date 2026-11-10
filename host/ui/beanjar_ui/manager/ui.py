# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The window's and dialogs' layouts: Blueprint files (host/ui/data/manager/*.blp), compiled by the
host build into one GResource bundle, beanjar.gresource, published to build/manager/.

template(name) is the Gtk.Template of ui/<name>.blp, for the class it decorates; icons(display)
adds the application icon (host/ui/data/icon.svg, named APP_ID) to display's icon theme (and, where the
desktop's icon theme is not installed, sets Adwaita: theme_fallback). Both come from
the first of:
- the directory given to use_dir() (python3 -m beanjar_ui.manager --ui-dir DIR), before the module with
  the templates (widgets.py) is imported: its <name>.ui files, or, where it has .blp files, all
  of them compiled now into a temporary directory by blueprint-compiler (the system's, on
  PATH). For trying out a layout without a build: --ui-dir host/ui/data/manager (the icon: host/ui/data/icon.svg of the checkout).
- $BEANJAR_GRESOURCE (the tests: the bundle in the build directory);
- build/manager/beanjar.gresource of the checkout this package is in.

host/ui/beanjar_ui/viewer/ui.py is the same for the viewer; keep the two in step.
"""

import atexit
import glob
import os
import shutil
import subprocess
import sys
import tempfile

import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gio, Gtk  # noqa: E402

APP_ID = "org.webosbrew.beanjar"
PREFIX = "/org/webosbrew/beanjar/"  # the application's resource base path (APP_ID's)
BUNDLE = "beanjar.gresource"
ENV = "BEANJAR_GRESOURCE"
# host/ui, or build/viewer as published: the directory beanjar_ui is in
PKG_PARENT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOP = os.path.dirname(os.path.dirname(PKG_PARENT))  # host/ui -> checkout
_state = {"dir": None, "bundle": None}


def blueprint_compiler():
    """The command of blueprint-compiler on PATH (Debian: blueprint-compiler)."""
    exe = shutil.which("blueprint-compiler")
    if exe:
        return [exe]
    raise SystemExit("ui: no blueprint-compiler on PATH (apt install blueprint-compiler)")


def use_dir(path):
    """Take the templates from PATH instead of the bundle: .ui files, or .blp files compiled now."""
    blps = sorted(glob.glob(os.path.join(path, "*.blp")))
    if blps:
        out = tempfile.mkdtemp(prefix="beanjar-ui.")
        atexit.register(shutil.rmtree, out, True)
        if subprocess.run(blueprint_compiler() + ["batch-compile", out, path] + blps).returncode:
            raise SystemExit(f"ui: blueprint-compiler failed on {path}")
        path = out
    _state["dir"] = path


def _load():
    if _state["bundle"]:
        return
    from beanjar import paths

    for path in (os.environ.get(ENV), paths.TREE.build("manager", BUNDLE)):
        if path and os.path.isfile(path):
            Gio.resources_register(Gio.Resource.load(path))
            _state["bundle"] = path
            return
    raise SystemExit(f"ui: no {BUNDLE}: the host build makes it (scripts/build.sh host)")


def app_icon_dir():
    """A temporary directory with host/ui/data/icon.svg as APP_ID.svg: the icon without the bundle."""
    out = tempfile.mkdtemp(prefix="beanjar-icon.")
    atexit.register(shutil.rmtree, out, True)
    icon = "/usr/share/icons/hicolor/scalable/apps/" + APP_ID + ".svg"
    os.symlink(
        icon if os.path.isfile(icon) else os.path.join(TOP, "host", "ui", "data", "icon.svg"),
        os.path.join(out, APP_ID + ".svg"),
    )
    return out


FALLBACK_THEME = "Adwaita"


def theme_fallback(display):
    """Where the desktop's icon theme (display's) is not installed - no <dir>/<name>/index.theme on
    the search path, e.g. the Pi's 'PiXtrix' - GTK resolves icons from hicolor alone and most
    symbolic ones vanish: use Adwaita for display instead (its Gtk.Settings; the display's theme
    itself takes no name). An installed theme is left alone. The name replaced, or None."""
    theme = Gtk.IconTheme.get_for_display(display)
    name = theme.get_theme_name()
    if any(os.path.isfile(os.path.join(d, name, "index.theme")) for d in theme.get_search_path()):
        return None
    Gtk.Settings.get_for_display(display).set_property("gtk-icon-theme-name", FALLBACK_THEME)
    print(f"ui: icon theme {name!r} is not installed; using {FALLBACK_THEME}", file=sys.stderr)
    return name


def icons(display):
    """The application icon (the bundle's icons/, or with use_dir host/ui/data/icon.svg) in display's
    icon theme, as APP_ID."""
    theme = Gtk.IconTheme.get_for_display(display)
    theme_fallback(display)
    if _state["dir"]:
        theme.add_search_path(app_icon_dir())
        return
    _load()
    theme.add_resource_path(PREFIX + "icons")


def template(name):
    if _state["dir"]:
        return Gtk.Template(filename=os.path.join(_state["dir"], name + ".ui"))
    _load()
    return Gtk.Template(resource_path=PREFIX + name + ".ui")
