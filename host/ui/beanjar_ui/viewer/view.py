# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""TvView: the guest picture in a Gtk.GLArea, drawn by the C core (docs/dev/architecture/viewer.md).

Pointer input on the area goes to the guest's tablet; keys are taken by the toplevel (window.py).
On a VM with a touch screen (vm-info.toml's touch), a touchscreen's touches go to it instead, each
contact a slot of its own; elsewhere they stay GTK's emulated pointer. There the mouse can be a
finger too (set_mouse_touch, the window's Mouse toggle): its left button's press, drag and release
are a touch's begin, moves and end, in a slot of its own; hovering sends nothing (a finger can't
hover), the wheel and the other buttons stay the pointer's.
Over the displayed picture the host pointer is always transparent; elsewhere
(black bars, the chrome) it is the desktop's arrow.
"""

import sys

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
gi.require_version("Graphene", "1.0")
from gi.repository import Gdk, GLib, Graphene, Gtk  # noqa: E402

from . import core as corelib  # noqa: E402


class TvView(Gtk.GLArea):
    def __init__(self, core, on_quit, touch=False):
        super().__init__()
        self.core = core
        self._on_quit = on_quit
        self._started = False
        self._blank = False
        # Supply real transparent pixels to the host compositor, including
        # remote desktops that do not honor the named "none" cursor.
        texture = Gdk.MemoryTexture.new(
            1, 1, Gdk.MemoryFormat.R8G8B8A8_PREMULTIPLIED, GLib.Bytes.new(bytes(4)), 4
        )
        self._transparent_cursor = Gdk.Cursor.new_from_texture(texture, 0, 0, None)
        self._pointer = None  # last logical pointer position over the area
        self._held = set()  # guest buttons held (0 left, 1 middle, 2 right)
        self.set_allowed_apis(Gdk.GLAPI.GLES)
        self.set_has_depth_buffer(False)
        self.set_has_stencil_buffer(False)
        self.set_auto_render(True)
        self.set_hexpand(True)
        self.set_vexpand(True)
        self.set_size_request(160, 90)
        self.set_focusable(False)
        self.connect("realize", self._realize)
        self.connect("render", self._render)

        motion = Gtk.EventControllerMotion()
        motion.connect("enter", self._motion)
        motion.connect("motion", self._motion)
        motion.connect("leave", self._leave)
        self.add_controller(motion)
        click = Gtk.GestureClick()
        click.set_button(0)  # all buttons
        click.connect("pressed", self._pressed)
        click.connect("released", self._released)
        click.connect("cancel", self._cancel)
        self.add_controller(click)
        self._touches = {}  # a touch's event sequence (MOUSE for the mouse's finger) -> its slot
        self.touch = touch
        self.mouse_touch = False  # the left button is a finger (set_mouse_touch)
        self._mouse_at = (0, 0)  # the mouse's finger: where it was last
        if touch:
            # before the gestures (capture phase), which would make the first touch a click
            raw = Gtk.EventControllerLegacy()
            raw.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
            raw.connect("event", self._touch)
            self.add_controller(raw)
        scroll = Gtk.EventControllerScroll.new(
            Gtk.EventControllerScrollFlags.VERTICAL | Gtk.EventControllerScrollFlags.DISCRETE
        )
        scroll.connect("scroll", self._scroll)
        self.add_controller(scroll)

    # ---- GL ----
    def _realize(self, _area):
        self.make_current()
        if self.get_error() is not None:
            print(f"viewer: no GL context: {self.get_error().message}", file=sys.stderr)
            self._on_quit()
            return
        self.core.callbacks(self._make_current, self._redraw, self._on_quit)
        self.core.view(self.get_width() or 1280, self.get_height() or 720)
        self.core.start()
        self.core.attach()
        self._started = True

    def _make_current(self):
        self.make_current()

    def _redraw(self):
        self.queue_render()
        self._update_cursor()

    def _render(self, _area, _context):
        if not self._started:
            return False
        scale = self.get_scale_factor()
        w, h = self.get_width(), self.get_height()
        self.core.view(w, h)
        self.core.render(w * scale, h * scale)
        self._update_cursor()
        return True

    # ---- pointer ----
    def _update_cursor(self):
        blank = self._started and self.core.pointer_blank()
        if blank != self._blank:
            self._blank = blank
            self.set_cursor(self._transparent_cursor if blank else None)
            if blank:
                self.core.pointer_blank_set()

    def _motion(self, _ctrl, x, y):
        self._pointer = (x, y)
        if self.mouse_touch and not self._held:
            return  # a finger can't hover: nothing until a button is down (a press carries its position)
        if self._started:
            self.core.view(self.get_width(), self.get_height())
            self.core.motion(round(x), round(y))
            self._update_cursor()

    def _leave(self, _ctrl):
        self._pointer = None
        if self._started:
            self.core.leave()
            self._update_cursor()

    @staticmethod
    def _guest_button(gesture):
        return {1: 0, 2: 1, 3: 2}.get(gesture.get_current_button())

    def _pressed(self, gesture, _n, x, y):
        b = self._guest_button(gesture)
        if b is None or not self._started:
            return
        self.core.button(b, True, round(x), round(y))
        self._held.add(b)

    def _released(self, gesture, _n, x, y):
        b = self._guest_button(gesture)
        if b is None or not self._started:
            return
        self.core.button(b, False, round(x), round(y))
        self._held.discard(b)

    def _cancel(self, _gesture, _sequence):
        # the implicit grab was taken away (another widget, a popover): release what is held
        if self._started and self._held:
            x, y = self._pointer or (0, 0)
            for b in list(self._held):
                self.core.button(b, False, round(x), round(y))
            self._held.clear()

    _TOUCH_KINDS = {
        Gdk.EventType.TOUCH_BEGIN: corelib.TOUCH_BEGIN,
        Gdk.EventType.TOUCH_UPDATE: corelib.TOUCH_UPDATE,
        Gdk.EventType.TOUCH_END: corelib.TOUCH_END,
        Gdk.EventType.TOUCH_CANCEL: corelib.TOUCH_CANCEL,
    }

    MOUSE = "mouse"  # _touches' key for the mouse's finger

    def set_mouse_touch(self, on):
        """The mouse's left button a finger on the touch screen (on) or the pointer's, as before. A
        finger the mouse holds down when it is turned off is lifted where it is."""
        on = bool(on and self.touch)
        if on == self.mouse_touch:
            return
        if not on and self.MOUSE in self._touches:
            slot = self._touches.pop(self.MOUSE)
            if self._started:
                x, y = self._mouse_at
                self.core.touch(corelib.TOUCH_END, slot, x, y)
        self.mouse_touch = on

    def _area_point(self, event):
        """The event's surface position in the area's logical coordinates, or None."""
        native = self.get_native()
        ok, sx, sy = event.get_position()
        if not ok or native is None:
            return None
        ox, oy = native.get_surface_transform()
        ok, p = native.compute_point(self, Graphene.Point().init(sx - ox, sy - oy))
        return (round(p.x), round(p.y)) if ok else None

    def _send_touch(self, kind, slot, at):
        self.core.view(self.get_width(), self.get_height())
        self.core.touch(kind, slot, at[0], at[1])

    def _new_slot(self, key):
        free = set(range(corelib.TOUCH_SLOTS)) - set(self._touches.values())
        if not free:
            return None  # more fingers than the panel's slots
        slot = self._touches[key] = min(free)
        return slot

    def _touch(self, ctrl, event):
        # PyGObject (3.50) hands the signal's GdkEvent over as None; the controller has it
        event = event or ctrl.get_current_event()
        if event is None:
            return False
        kind = self._TOUCH_KINDS.get(event.get_event_type())
        if kind is None:
            return self._mouse_finger(event)
        if not self._started:
            return True
        seq = event.get_event_sequence()
        slot = self._touches.get(seq)
        if kind == corelib.TOUCH_BEGIN and slot is None:
            slot = self._new_slot(seq)
        if slot is None:
            return True
        at = self._area_point(event)
        if at:
            self._send_touch(kind, slot, at)
        if kind in (corelib.TOUCH_END, corelib.TOUCH_CANCEL):
            self._touches.pop(seq, None)
        return True

    def _mouse_finger(self, event):
        """The mouse as a finger (mouse_touch): the left button's press a touch's begin, motion
        while it is down its moves, its release the end; True for the events it takes, which then
        reach neither the click gesture nor the motion controller (no pointer button or motion)."""
        if not self.mouse_touch or not self._started:
            return False
        device = event.get_device()
        if device is not None and device.get_source() == Gdk.InputSource.TOUCHSCREEN:
            return False  # a touchscreen's emulated pointer: its touches are taken above
        t = event.get_event_type()
        slot = self._touches.get(self.MOUSE)
        if t == Gdk.EventType.BUTTON_PRESS and event.get_button() == 1:
            at = self._area_point(event)
            if slot is not None or at is None:
                return True
            slot = self._new_slot(self.MOUSE)
            if slot is not None:
                self._mouse_at = at
                self._send_touch(corelib.TOUCH_BEGIN, slot, at)
            return True
        if slot is None:
            return False
        if t == Gdk.EventType.MOTION_NOTIFY:
            at = self._area_point(event)
            if at:
                self._mouse_at = at
                self._send_touch(corelib.TOUCH_UPDATE, slot, at)
            return True
        if t == Gdk.EventType.BUTTON_RELEASE and event.get_button() == 1:
            at = self._area_point(event) or self._mouse_at
            self._touches.pop(self.MOUSE, None)
            self._send_touch(corelib.TOUCH_END, slot, at)
            return True
        if t == Gdk.EventType.GRAB_BROKEN:
            self._touches.pop(self.MOUSE, None)
            self._send_touch(corelib.TOUCH_CANCEL, slot, self._mouse_at)
        return False

    def _scroll(self, _ctrl, _dx, dy):
        if self._started and dy:
            steps = -int(dy) if abs(dy) >= 1 else (-1 if dy > 0 else 1)
            self.core.wheel(steps)
        return True

    def stopped(self):
        """The core is finished: draw nothing more, leave the pointer alone."""
        self._started = False
        self.set_cursor(None)

    # ---- helpers for the window ----
    def screenshot(self, path):
        """The guest picture at the area's size (letterboxed as shown, no chrome), as PPM."""
        scale = self.get_scale_factor()
        return self.core.screenshot(path, self.get_width() * scale, self.get_height() * scale)

    def picture_rect(self):
        return self.core.picture()
