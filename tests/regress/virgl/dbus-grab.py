#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Grab the guest scanout the way the host viewer will: from QEMU's dbus display.

    dbus-grab.py [--dir VM-DIR | --bus ADDRESS] [--raw] [--straight-alpha] OUT.png [WAIT_SECONDS]

The display is the checkout VM's (unix:path=RUN_DIR/dbus.sock, RUN_DIR default build), a library
VM's with --dir (VM-DIR/run/dbus.sock, as host/scripts/vm-launcher --dir), or --bus ADDRESS.

Registers an org.qemu.Display1.Listener on Console_0 over a peer-to-peer connection, collects
ScanoutDMABUF calls for WAIT_SECONDS (default 3), then imports the most recent dmabuf into EGL on
the host render node (EGL_EXT_image_dma_buf_import, with its modifier), reads it back with
glReadPixels and writes a PNG. It prints the dmabuf's fourcc and modifier and the RGBA of the
regions drawn by kms-gles.

By default the PNG is what the screen shows: the UI scanout composited over an opaque black video
plane (a TV's video plane is black when nothing plays), as premultiplied alpha like the viewer
(host/viewer, --straight-alpha as its option), so an opaque RGB PNG. Video frames are not drawn:
they arrive on the media sockets, which take one client and need decoding; the viewer's
`--headless --screenshot` composites them. --raw writes the scanout's own RGBA instead (alpha
kept), and when the fourcc has no alpha (XR24/XB24) also imports the same buffer as the matching
alpha format, OUT-as-<fourcc>.png, to show whether the alpha bytes survived in memory.

It also implements org.qemu.Display1.Listener.Unix.Map, which the host build's patched QEMU uses
when its EGL display cannot export dmabufs (surfaceless d3d12 on WSL2): then the last ScanoutMap'd
surface (a8r8g8b8 in shared memory, top row first, alpha kept) is written, with no EGL at all.

Uses only what Debian's python3-gi, python3-pil and the Mesa runtime provide (EGL/GLES through
ctypes). Run it on the VM host, against a QEMU started with -display dbus,gl=on,addr=...
"""

import argparse
import ctypes as C
import mmap
import os
import socket
import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402
from PIL import Image  # noqa: E402

LISTENER_XML = """
<node><interface name="org.qemu.Display1.Listener">
  <method name="Scanout"><arg type="u" direction="in"/><arg type="u" direction="in"/>
    <arg type="u" direction="in"/><arg type="u" direction="in"/><arg type="ay" direction="in"/></method>
  <method name="Update"><arg type="i" direction="in"/><arg type="i" direction="in"/>
    <arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="u" direction="in"/>
    <arg type="u" direction="in"/><arg type="ay" direction="in"/></method>
  <method name="ScanoutDMABUF"><arg type="h" direction="in"/><arg type="u" direction="in"/>
    <arg type="u" direction="in"/><arg type="u" direction="in"/><arg type="u" direction="in"/>
    <arg type="t" direction="in"/><arg type="b" direction="in"/></method>
  <method name="UpdateDMABUF"><arg type="i" direction="in"/><arg type="i" direction="in"/>
    <arg type="i" direction="in"/><arg type="i" direction="in"/></method>
  <method name="Disable"/>
  <method name="MouseSet"><arg type="i" direction="in"/><arg type="i" direction="in"/>
    <arg type="i" direction="in"/></method>
  <method name="CursorDefine"><arg type="i" direction="in"/><arg type="i" direction="in"/>
    <arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="ay" direction="in"/></method>
  <property name="Interfaces" type="as" access="read"/>
</interface>
<interface name="org.qemu.Display1.Listener.Unix.Map">
  <method name="ScanoutMap"><arg type="h" direction="in"/><arg type="u" direction="in"/>
    <arg type="u" direction="in"/><arg type="u" direction="in"/><arg type="u" direction="in"/>
    <arg type="u" direction="in"/></method>
  <method name="UpdateMap"><arg type="i" direction="in"/><arg type="i" direction="in"/>
    <arg type="i" direction="in"/><arg type="i" direction="in"/></method>
</interface></node>"""


def fourcc_str(f):
    return f.to_bytes(4, "little").decode("latin1")


def fourcc(s):
    return int.from_bytes(s.encode(), "little")


class Grabber:
    def __init__(self):
        self.dmabuf = None  # (fd, width, height, stride, fourcc, modifier, y0_top)
        self.map = None  # (mmap, offset, width, height, stride, pixman format)
        self.counts = {}
        self.last_desc = None

    def method_call(self, conn, sender, path, iface, method, params, invocation):
        self.counts[method] = self.counts.get(method, 0) + 1
        if method == "ScanoutDMABUF":
            h, w, hgt, stride, fcc, mod, y0 = params.unpack()
            fd = invocation.get_message().get_unix_fd_list().get(h)
            if self.dmabuf:
                os.close(self.dmabuf[0])
            self.dmabuf = (fd, w, hgt, stride, fcc, mod, y0)
            desc = (
                f"ScanoutDMABUF {w}x{hgt} stride {stride} fourcc {fourcc_str(fcc)} "
                f"modifier {mod:#x} y0_top {y0}"
            )
            if desc != self.last_desc:
                print(f"{desc} (from call {self.counts[method]})")
                self.last_desc = desc
        elif method == "ScanoutMap":
            h, offset, w, hgt, stride, fmt = params.unpack()
            fd = invocation.get_message().get_unix_fd_list().get(h)
            m = mmap.mmap(fd, offset + stride * hgt, mmap.MAP_SHARED, mmap.PROT_READ)
            os.close(fd)
            self.map = (m, offset, w, hgt, stride, fmt)
            desc = f"ScanoutMap {w}x{hgt} stride {stride} pixman format {fmt:#x}"
            if desc != self.last_desc:
                print(f"{desc} (from call {self.counts[method]})")
                self.last_desc = desc
        elif method == "Scanout":
            w, hgt, stride, fmt, _ = params.unpack()
            if self.counts[method] == 1:
                print(f"Scanout (2D surface) {w}x{hgt} pixman format {fmt:#x}")
        invocation.return_value(None)

    def get_property(self, conn, sender, path, iface, prop):
        return GLib.Variant("as", ["org.qemu.Display1.Listener.Unix.Map"])


# --- EGL/GLES through ctypes ---
egl = C.CDLL("libEGL.so.1")
gles = C.CDLL("libGLESv2.so.2")
egl.eglGetProcAddress.restype = C.c_void_p
egl.eglGetProcAddress.argtypes = [C.c_char_p]
egl.eglGetPlatformDisplay.restype = C.c_void_p
egl.eglGetPlatformDisplay.argtypes = [C.c_uint, C.c_void_p, C.c_void_p]
egl.eglInitialize.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
egl.eglBindAPI.argtypes = [C.c_uint]
egl.eglCreateContext.restype = C.c_void_p
egl.eglCreateContext.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_int)]
egl.eglMakeCurrent.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p]
egl.eglGetError.restype = C.c_int
egl.eglQueryString.restype = C.c_char_p
egl.eglQueryString.argtypes = [C.c_void_p, C.c_int]
gles.glGetString.restype = C.c_char_p
create_image = C.CFUNCTYPE(
    C.c_void_p, C.c_void_p, C.c_void_p, C.c_uint, C.c_void_p, C.POINTER(C.c_int)
)(egl.eglGetProcAddress(b"eglCreateImageKHR"))
image_target = C.CFUNCTYPE(None, C.c_uint, C.c_void_p)(
    egl.eglGetProcAddress(b"glEGLImageTargetTexture2DOES")
)


def egl_setup():
    dpy = egl.eglGetPlatformDisplay(0x31DD, None, None)  # EGL_PLATFORM_SURFACELESS_MESA
    if not dpy or not egl.eglInitialize(dpy, None, None):
        sys.exit(f"eglInitialize failed {egl.eglGetError():#x}")
    egl.eglBindAPI(0x30A0)  # EGL_OPENGL_ES_API
    attrs = (C.c_int * 3)(0x3098, 2, 0x3038)  # EGL_CONTEXT_CLIENT_VERSION 2
    ctx = egl.eglCreateContext(dpy, None, None, attrs)  # EGL_KHR_no_config_context
    if not ctx or not egl.eglMakeCurrent(dpy, None, None, ctx):
        sys.exit(f"context failed {egl.eglGetError():#x}")
    print("host GL_RENDERER", gles.glGetString(0x1F01).decode())
    return dpy


def read_dmabuf(dpy, fd, w, h, stride, fcc, mod):
    a = [0x3057, w, 0x3056, h, 0x3271, fcc, 0x3272, fd, 0x3273, 0, 0x3274, stride]
    if mod != 0x00FFFFFFFFFFFFFF:  # DRM_FORMAT_MOD_INVALID: let the driver guess
        a += [0x3443, mod & 0xFFFFFFFF, 0x3444, mod >> 32]
    a.append(0x3038)
    img = create_image(dpy, None, 0x3270, None, (C.c_int * len(a))(*a))  # EGL_LINUX_DMA_BUF_EXT
    if not img:
        print(f"  import as {fourcc_str(fcc)} failed, EGL error {egl.eglGetError():#x}")
        return None
    tex, fbo = C.c_uint(), C.c_uint()
    gles.glGenTextures(1, C.byref(tex))
    gles.glBindTexture(0x0DE1, tex)
    image_target(0x0DE1, img)
    gles.glGenFramebuffers(1, C.byref(fbo))
    gles.glBindFramebuffer(0x8D40, fbo)
    gles.glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, tex, 0)
    if gles.glCheckFramebufferStatus(0x8D40) != 0x8CD5:
        print("  framebuffer incomplete")
        return None
    buf = C.create_string_buffer(w * h * 4)
    gles.glReadPixels(0, 0, w, h, 0x1908, 0x1401, buf)  # GL_RGBA, GL_UNSIGNED_BYTE
    return Image.frombuffer("RGBA", (w, h), buf.raw, "raw", "RGBA", 0, 1)


def peer_auth(sock):
    """SASL on the listener socket, as host/viewer/core/peer.c does: QEMU is the server and a stock
    QEMU offers only EXTERNAL and DBUS_COOKIE_SHA1, for which GDBus's client claims our uid. In a
    rootless user namespace (mode=mitm) QEMU sees us as uid 0 and rejects that, so claim an empty
    identity (whatever the socket credentials say), then the classic uid for servers that want it."""
    f = sock.makefile("rb", buffering=0)

    def say(line):
        sock.sendall(line.encode() + b"\r\n")
        return f.readline().decode().rstrip()

    sock.sendall(b"\0")
    reply = say("AUTH EXTERNAL")
    if reply == "DATA":
        reply = say("DATA")
    if not reply.startswith("OK "):
        reply = say("AUTH EXTERNAL " + str(os.getuid()).encode().hex())
    if not reply.startswith("OK "):
        sys.exit(f"peer auth failed: {reply}")
    if say("NEGOTIATE_UNIX_FD") != "AGREE_UNIX_FD":
        sys.exit("peer refused fd passing")
    sock.sendall(b"BEGIN\r\n")


def args():
    p = argparse.ArgumentParser(
        prog="dbus-grab.py", description="screenshot of a VM's dbus display"
    )
    where = p.add_mutually_exclusive_group()
    where.add_argument("--dir", help="a VM directory (the manager's layout): its run/dbus.sock")
    where.add_argument("--bus", help="the display's D-Bus address (default: the checkout VM's)")
    p.add_argument(
        "--raw", action="store_true", help="the scanout's RGBA, not composited over black"
    )
    p.add_argument("--straight-alpha", action="store_true", help="composite as straight alpha")
    p.add_argument("out", help="the PNG to write")
    p.add_argument("wait", nargs="?", type=float, default=3, help="seconds to collect scanouts (3)")
    a = p.parse_args()
    if a.dir:
        a.bus = "unix:path=" + os.path.join(os.path.abspath(a.dir), "run", "dbus.sock")
    elif not a.bus:
        top = os.path.dirname(
            os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
        )
        a.bus = "unix:path=" + os.path.join(top, os.environ.get("RUN_DIR") or "build", "dbus.sock")
    return a


def screen(im, straight):
    """What the TV shows: the UI over an opaque black video plane, blended as the viewer does
    (premultiplied: src + dst * (1 - a), so over black the colour as stored; straight: src * a)."""
    if straight:
        return Image.alpha_composite(Image.new("RGBA", im.size, (0, 0, 0, 255)), im).convert("RGB")
    return im.convert("RGB")  # drops the alpha: the stored colour


def save(im, out, a):
    if a.raw:
        im.save(out)
        return
    screen(im, a.straight_alpha).save(out)


def main():
    a = args()
    addr, out, wait = a.bus, a.out, a.wait
    bus = Gio.DBusConnection.new_for_address_sync(
        addr,
        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
        | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
        None,
        None,
    )
    mine, theirs = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    fds = Gio.UnixFDList.new()
    fds.append(theirs.fileno())
    bus.call_with_unix_fd_list_sync(
        "org.qemu",
        "/org/qemu/Display1/Console_0",
        "org.qemu.Display1.Console",
        "RegisterListener",
        GLib.Variant("(h)", (0,)),
        None,
        Gio.DBusCallFlags.NONE,
        -1,
        fds,
        None,
    )
    theirs.close()
    peer_auth(mine)
    stream = Gio.Socket.new_from_fd(mine.detach()).connection_factory_create_connection()
    conn = Gio.DBusConnection.new_sync(
        stream, None, Gio.DBusConnectionFlags.DELAY_MESSAGE_PROCESSING, None, None
    )
    g = Grabber()
    for info in Gio.DBusNodeInfo.new_for_xml(LISTENER_XML).interfaces:
        conn.register_object(
            "/org/qemu/Display1/Listener", info, g.method_call, g.get_property, None
        )
    conn.start_message_processing()
    loop = GLib.MainLoop()
    GLib.timeout_add(int(wait * 1000), loop.quit)
    loop.run()
    print("calls:", g.counts)
    if g.map and not g.dmabuf:
        m, offset, w, h, stride, fmt = g.map
        if fmt not in (0x20028888, 0x20020888):  # a8r8g8b8, x8r8g8b8: BGRA/BGRX in memory
            sys.exit(f"unsupported pixman format {fmt:#x}")
        im = Image.frombuffer(
            "RGBA", (w, h), m[offset : offset + stride * h], "raw", "BGRA", stride, 1
        )
        if fmt == 0x20020888:
            im.putalpha(255)
        save(im, out, a)
        print(f"mapped surface -> {out}" + ("" if a.raw else " (over black)"))
        alpha = im.getchannel("A").getextrema()
        print(f"  alpha min/max {alpha}; centre RGBA = {im.getpixel((w // 2, h // 2))}")
        return
    if not g.dmabuf:
        sys.exit("no ScanoutDMABUF or ScanoutMap received")

    fd, w, h, stride, fcc, mod, y0 = g.dmabuf
    dpy = egl_setup()
    xs = {"background": 10, "red a=255": w // 6, "green a=128": w // 2, "blue a=0": w * 5 // 6}
    variants = [fcc]
    alpha_of = {fourcc("XR24"): fourcc("AR24"), fourcc("XB24"): fourcc("AB24")}
    if a.raw and fcc in alpha_of:
        variants.append(alpha_of[fcc])
    for i, f in enumerate(variants):
        im = read_dmabuf(dpy, fd, w, h, stride, f, mod)
        if im is None:
            continue
        # Rows come out in memory order. QEMU's own GL UIs (gtk-egl) blit the texture into a
        # bottom-up GL window flipped when y0_top is false, i.e. memory row 0 is the top of the
        # picture then; virgl scanouts report y0_top false. So only y0_top true needs a flip.
        if y0:
            im = im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        name = out if i == 0 else out.replace(".png", f"-as-{fourcc_str(f)}.png")
        save(im, name, a)
        print(f"imported as {fourcc_str(f)} -> {name}" + ("" if a.raw else " (over black)"))
        for k, x in xs.items():
            print(f"  {k:12s} RGBA = {im.getpixel((x, h // 2))}")


main()
