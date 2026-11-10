# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The TV remote's window: borderless, transparent around the body, input
only on the body, dragged by any point of the body that is not a key or
control. Placed right of the TV window where X11 allows; transient for it.
Never docked (a user decision)."""

import ctypes
import math

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
gi.require_version("GdkPixbuf", "2.0")
gi.require_version("Adw", "1")
from gi.repository import Gdk, Gio, GLib, Gtk  # noqa: E402

from . import ui  # noqa: E402
from .keys import key_controller  # noqa: E402
from .remote import LAYOUTS, REMOTE_W, RemotePanel, window_scale  # noqa: E402


class _Region:
    """gdk_surface_set_input_region through ctypes: PyGObject needs python3-gi-cairo for a
    cairo.Region, which the Pi does not have."""

    lib = None

    @classmethod
    def set(cls, surface, rects):
        if cls.lib is None:
            P = ctypes.c_void_p
            gtk, cairo = ctypes.CDLL("libgtk-4.so.1"), ctypes.CDLL("libcairo.so.2")
            cairo.cairo_region_create.restype = P
            cairo.cairo_region_union_rectangle.argtypes = [P, ctypes.POINTER(ctypes.c_int * 4)]
            cairo.cairo_region_destroy.argtypes = [P]
            gtk.gdk_surface_set_input_region.argtypes = [P, P]
            cls.lib = (gtk, cairo)
        gtk, cairo = cls.lib
        region = cairo.cairo_region_create()
        for r in rects:
            cairo.cairo_region_union_rectangle(region, ctypes.byref((ctypes.c_int * 4)(*r)))
        get = ctypes.pythonapi.PyCapsule_GetPointer
        get.restype, get.argtypes = ctypes.c_void_p, [ctypes.py_object, ctypes.c_char_p]
        gtk.gdk_surface_set_input_region(get(surface.__gpointer__, None), region)
        cairo.cairo_region_destroy(region)


class _X11:
    """XMoveWindow through ctypes on a connection of our own: GTK 4 has no call to position a
    toplevel. Only on X11; None where it cannot be done."""

    lib = None
    dpys = {}

    @classmethod
    def _xid(cls, surface):
        try:
            gi.require_version("GdkX11", "4.0")
            from gi.repository import GdkX11
        except (ImportError, ValueError):
            return None
        if not isinstance(surface, GdkX11.X11Surface):
            return None
        return GdkX11.X11Surface.get_xid(surface)

    @classmethod
    def _dpy(cls, display):
        if cls.lib is None:
            x11 = ctypes.CDLL("libX11.so.6")
            x11.XOpenDisplay.restype = ctypes.c_void_p
            x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
            x11.XDefaultRootWindow.restype = ctypes.c_ulong
            x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
            x11.XTranslateCoordinates.argtypes = [
                ctypes.c_void_p,
                ctypes.c_ulong,
                ctypes.c_ulong,
                ctypes.c_int,
                ctypes.c_int,
                ctypes.POINTER(ctypes.c_int),
                ctypes.POINTER(ctypes.c_int),
                ctypes.POINTER(ctypes.c_ulong),
            ]
            x11.XMoveWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int]
            x11.XFlush.argtypes = [ctypes.c_void_p]
            cls.lib = x11
        name = display.get_name()
        if name not in cls.dpys:
            cls.dpys[name] = cls.lib.XOpenDisplay(name.encode())
        return cls.dpys[name]

    @classmethod
    def available(cls, display):
        """Whether windows on display can be placed (an X11 display)."""
        try:
            gi.require_version("GdkX11", "4.0")
            from gi.repository import GdkX11
        except (ImportError, ValueError):
            return False
        return isinstance(display, GdkX11.X11Display)

    @classmethod
    def place_right_of(cls, window, anchor, gap=16):
        """Move window's surface right of anchor's content, top edges aligned. False if not X11."""
        try:
            xid, anchor_xid = cls._xid(window.get_surface()), cls._xid(anchor.get_surface())
            if not xid or not anchor_xid:
                return False
            dpy = cls._dpy(window.get_display())
            if not dpy:
                return False
            x11 = cls.lib
            rx, ry, child = ctypes.c_int(), ctypes.c_int(), ctypes.c_ulong()
            x11.XTranslateCoordinates(
                dpy,
                anchor_xid,
                x11.XDefaultRootWindow(dpy),
                0,
                0,
                ctypes.byref(rx),
                ctypes.byref(ry),
                ctypes.byref(child),
            )
            scale = anchor.get_surface().get_scale_factor()
            sx, sy = anchor.get_surface_transform()  # client-side shadow, if any
            right = anchor.get_surface().get_width() - sx  # the shadow is the same on both sides
            x11.XMoveWindow(
                dpy, xid, round(rx.value + (right + gap) * scale), round(ry.value + sy * scale)
            )
            x11.XFlush(dpy)
            return True
        except (OSError, AttributeError, TypeError):
            return False


@ui.template("remote-window")
class RemoteWindow(Gtk.Window):
    """The TV remote's window: borderless, transparent around the body, input only on the body,
    moved by dragging the body. A close request (the close control on its chin is window.close)
    and Escape hide it (the Menu key or the Remote toggle shows it again); the chin's menu is remote_menu, with the
    actions remote.layout (the layout's name: "normal" or "service") and remote.place
    (enabled on X11 only)."""

    __gtype_name__ = "ViewerRemoteWindow"
    remote_menu = Gtk.Template.Child()

    def __init__(self, pane):
        super().__init__()
        self.pane = pane
        self._start = None
        self.moves = 0  # begin_move calls (tests)
        self.shape = None  # the input region last set, surface coordinates
        self.placed = None  # None: not yet shown; then whether it was put by the TV
        self._fit()
        pane.view.connect("resize", lambda *_: self._fit())
        # The Remote toggle follows the window however it is shown, hidden or minimized.
        for prop in ("visible", "suspended"):
            self.connect(f"notify::{prop}", lambda *_: pane.sync_remote_toggle())
        self.add_controller(key_controller(pane, escape=self.hide_remote))
        actions = Gio.SimpleActionGroup()
        layout = Gio.SimpleAction.new_stateful(
            "layout", GLib.VariantType.new("s"), GLib.Variant.new_string(pane.remote.layout.name)
        )
        layout.connect("change-state", self._layout_changed)
        actions.add_action(layout)
        place = Gio.SimpleAction.new("place", None)
        place.connect("activate", lambda *_: self._place())
        place.set_enabled(_X11.available(self.get_display()))
        actions.add_action(place)
        self.insert_action_group("remote", actions)
        choices = Gio.Menu()
        for name in pane.remote_layouts:
            item = Gio.MenuItem.new(LAYOUTS[name].title, "remote.layout")
            item.set_attribute_value("target", GLib.Variant.new_string(name))
            choices.append_item(item)
        self.menu_model = Gio.Menu()
        self.menu_model.append_section(None, choices)
        for i in range(self.remote_menu.get_n_items()):
            self.menu_model.append_section(
                None, self.remote_menu.get_item_link(i, Gio.MENU_LINK_SECTION)
            )
        pane.remote.set_floating(self.menu_model)
        pane.remote.on_layout = self._layout
        self.set_child(pane.remote)

    @Gtk.Template.Callback()
    def on_active_changed(self, _w, _pspec):
        if not self.is_active():
            self.pane.release()

    def _fit(self):
        """The window's size for the panel's layout beside the TV picture (remote.window_scale),
        again whenever the picture's size changes."""
        layout = self.pane.remote.layout
        s = window_scale(self.pane.view.get_height(), layout)
        size = (round(REMOTE_W * s), round(layout.height * s))
        if self.get_default_size() != size:
            self.set_default_size(*size)

    def _layout_changed(self, action, value):
        name = value.get_string()
        if name not in self.pane.remote_layouts:
            return
        action.set_state(value)
        self.pane.set_remote_layout(name)
        self._fit()
        self.shape = None
        self._layout()

    def hide_remote(self):
        GLib.idle_add(lambda: (self.pane.show_remote(False), False)[1])

    @Gtk.Template.Callback()
    def on_close_request(self, _w):
        self.hide_remote()
        return True

    # ---- moving ----
    @Gtk.Template.Callback()
    def on_drag_begin(self, gesture, x, y):
        target = self.pick(x, y, Gtk.PickFlags.DEFAULT)
        if self.pane.remote.menu_open() or self.pane.remote.is_control(target):
            gesture.set_state(Gtk.EventSequenceState.DENIED)
            self._start = None
            return
        self._start = (x, y)

    @Gtk.Template.Callback()
    def on_drag_update(self, gesture, dx, dy):
        if self._start is None:
            return
        threshold = self.get_settings().get_property("gtk-dnd-drag-threshold")
        if math.hypot(dx, dy) < threshold:
            return
        x, y = self._start
        self._start = None
        surface = self.get_surface()
        if not isinstance(surface, Gdk.Toplevel):
            return
        sx, sy = self.get_surface_transform()
        gesture.set_state(Gtk.EventSequenceState.CLAIMED)
        self.moves += 1
        surface.begin_move(
            gesture.get_device(),
            gesture.get_current_button(),
            x + sx,
            y + sy,
            gesture.get_current_event_time(),
        )
        gesture.reset()

    # ---- shape and place ----
    @Gtk.Template.Callback()
    def on_map(self, _w):  # set the shape again after hide/show
        self.shape = None
        self._layout()
        if self.placed is None:  # first shown: next to the TV window where X11 allows
            GLib.idle_add(self._place)

    def _place(self):
        tv = self.pane.get_root()
        self.placed = (
            isinstance(tv, Gtk.Window) and tv.get_mapped() and _X11.place_right_of(self, tv)
        )
        return False

    def _layout(self):
        GLib.idle_add(self._update_shape)

    def _update_shape(self):
        panel, surface = self.get_child(), self.get_surface()
        if not isinstance(panel, RemotePanel) or surface is None:
            return False
        r = panel.translate_coordinates(self, 0, 0)  # (x, y) or (ok, x, y) by bindings
        sx, sy = self.get_surface_transform()
        dx, dy = round(r[-2] + sx), round(r[-1] + sy)
        shape = [(x + dx, y + dy, w, h) for x, y, w, h in panel.body_rects()]
        if shape and shape != self.shape:
            self.shape = shape
            _Region.set(surface, shape)
        return False
