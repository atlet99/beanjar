# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Capture-phase key handling shared by the viewer's windows (window.py,
remote_window.py): every key goes to the guest, before GTK's own handling.
The Menu key toggles the remote, Escape in the remote's window hides it, F11
toggles fullscreen; the core sends the remote's own functions as IR buttons
and the rest to the virtio keyboard."""

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
gi.require_version("GdkPixbuf", "2.0")
gi.require_version("Adw", "1")
from gi.repository import Gtk  # noqa: E402

KEY_COMPOSE = 127  # the PC Menu (Application) key, evdev
KEY_ESC = 1
KEY_F11 = 87  # fullscreen: the viewer's, not sent
HOME_KEYS = (102, 125, 126)  # Home and left/right Super, evdev
EVDEV_OFFSET = 8  # X11 and Wayland hardware keycodes are evdev + 8
MENU_REPEAT_MS = 1000  # a Menu press within this of the last, not released: auto-repeat


def key_controller(pane, escape=None):
    """Capture-phase key handling for a toplevel of the viewer. escape: called for Escape instead
    of sending it (the remote's window)."""
    ctrl = Gtk.EventControllerKey()
    ctrl.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)

    def pressed(c, _keyval, keycode, _state):
        if pane.menu_open():
            return False  # the menu popover is open: it gets the keyboard
        code = keycode - EVDEV_OFFSET
        if code == KEY_COMPOSE:
            # Auto-repeat toggles nothing. The state is the pane's: the remote's window shows or
            # hides under the key, so its repeats and release may land in the other window, or
            # (the release) nowhere; a press long after the last one is new even without it.
            t, last = c.get_current_event_time(), pane.menu_down
            pane.menu_down = t
            if last is None or not 0 <= t - last < MENU_REPEAT_MS:
                pane.toggle_remote()
            return True
        if escape and code == KEY_ESC:
            escape()
            return True
        if code == KEY_F11 and pane.on_fullscreen:
            if not pane.f11_down:  # not its auto-repeat
                pane.f11_down = True
                pane.on_fullscreen()
            return True
        pane.key_evdev(code, True)
        pane.keys += 1
        return True

    def released(_c, _keyval, keycode, _state):
        code = keycode - EVDEV_OFFSET
        if code == KEY_COMPOSE:
            pane.menu_down = None
            return
        if escape and code == KEY_ESC:
            return
        if code == KEY_F11 and pane.on_fullscreen:
            pane.f11_down = False
            return
        pane.key_evdev(code, False)

    ctrl.connect("key-pressed", pressed)
    ctrl.connect("key-released", released)
    return ctrl
