# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Beanjar's desktop windows (GTK 4, libadwaita): the VM manager's (manager) and the viewer's
(viewer), over the core package beanjar (host/core)."""


def no_display(prog, instead):
    """None where GTK has a display to open windows on, else the one line to exit with (prog: no
    display ...; instead: what to do without one). GTK has opened the default display by the time
    its module is imported; without a Wayland or X11 session there is none, and the first call
    that needs one would fail with a TypeError from deep in PyGObject."""
    from gi.repository import Gdk

    if Gdk.Display.get_default() is not None:
        return None
    return f"{prog}: no display to open a window on; run it in a desktop session, or {instead}"
