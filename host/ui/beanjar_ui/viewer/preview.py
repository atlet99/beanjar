# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""--remote-preview OUT.ppm [--remote-layout normal|service] [--remote-press KEY] [--seconds N]:
the remote panel alone, no VM, at 2x its layout (376x1304 normal, 376x1528 service), rendered
through GTK and written as PPM. Needs a display
(xvfb-run -a in scripts). Clicks show but send nothing; the chin's controls are drawn but inert."""

import ctypes
import sys

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import GLib, Gtk  # noqa: E402

from .remote import LAYOUTS, REMOTE_W, RemotePanel  # noqa: E402


def _gpointer(obj):
    get = ctypes.pythonapi.PyCapsule_GetPointer
    get.restype, get.argtypes = ctypes.c_void_p, [ctypes.py_object, ctypes.c_char_p]
    return get(obj.__gpointer__, None)


def render_widget(widget, w, h, path):
    """The widget as it is now, through its native's GSK renderer, to PPM. PyGObject (3.42)
    cannot carry GskRenderNode, so the few GTK calls go through ctypes."""
    gtk = ctypes.CDLL("libgtk-4.so.1")
    P = ctypes.c_void_p
    for name, res, args in (
        ("gtk_snapshot_new", P, []),
        ("gdk_paintable_snapshot", None, [P, P, ctypes.c_double, ctypes.c_double]),
        ("gtk_snapshot_free_to_node", P, [P]),
        ("gsk_renderer_render_texture", P, [P, P, P]),
        ("gdk_texture_get_width", ctypes.c_int, [P]),
        ("gdk_texture_get_height", ctypes.c_int, [P]),
        ("gdk_texture_download", None, [P, P, ctypes.c_size_t]),
        ("gsk_render_node_unref", None, [P]),
        ("g_object_unref", None, [P]),
    ):
        f = getattr(gtk, name)
        f.restype, f.argtypes = res, args
    paintable = Gtk.WidgetPaintable.new(widget)
    snapshot = gtk.gtk_snapshot_new()
    gtk.gdk_paintable_snapshot(_gpointer(paintable), snapshot, w, h)
    node = gtk.gtk_snapshot_free_to_node(snapshot)
    renderer = widget.get_native().get_renderer()
    texture = gtk.gsk_renderer_render_texture(_gpointer(renderer), node, None)
    tw, th = gtk.gdk_texture_get_width(texture), gtk.gdk_texture_get_height(texture)
    data = (ctypes.c_ubyte * (tw * th * 4))()
    gtk.gdk_texture_download(texture, data, tw * 4)  # cairo ARGB32: B, G, R, A in memory
    gtk.g_object_unref(texture)
    gtk.gsk_render_node_unref(node)
    raw = bytes(data)
    rgb = bytearray(tw * th * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (tw, th))
        f.write(rgb)


def remote_preview(path, press, seconds, layout="normal"):
    w, h = REMOTE_W * 2, LAYOUTS[layout].height * 2
    app = Gtk.Application(application_id=None)
    state = {"ok": False}

    def activate(app):
        win = Gtk.Window(application=app, title="Remote")
        panel = RemotePanel(send=None, layout=layout)
        if press:
            try:
                panel.show_held(press)
            except ValueError as e:
                print(f"viewer: {e}", file=sys.stderr)
                app.quit()
                return
        panel.set_size_request(w, h)
        win.set_child(panel)
        win.set_resizable(False)
        win.present()

        def grab():
            if panel.get_width() < w or panel.get_height() < h:
                return True
            render_widget(panel, w, h, path)
            state["ok"] = True
            win.close()
            return False

        GLib.timeout_add(max(200, int(seconds * 1000)), grab)

    app.connect("activate", activate)
    app.run([])
    if not state["ok"]:
        print(f"remote closed; no {path}", file=sys.stderr)
        return 1
    return 0
