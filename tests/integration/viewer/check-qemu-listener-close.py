#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The tree's QEMU frees a D-Bus display listener whose connection closes.

check-qemu-listener-close.py QEMU DISPLAY1_XML

QEMU (build/qemu's, no guest, CPUs stopped) on a private bus; a listener registers on Console_0
and closes its connection, three times. Each listener's DisplayChangeListener must be unregistered
when its connection closes (QEMU's displaychangelistener_unregister trace), and nothing reported.
Before the patched QEMU's patch 0004 (beanjar-vendor's qemu/patches) the connection's filter held a reference on the
listener, so it was never freed, and every later frame was sent to the closed connection:
"Failed to call update: The connection is closed" per frame and listener, for as long as QEMU ran.
"""

import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

CYCLES = 3
LISTENER = "/org/qemu/Display1/Listener"
IFACES = ["org.qemu.Display1.Listener", "org.qemu.Display1.Listener.Unix.Map"]


def wait_for(pred, timeout, what):
    end = time.monotonic() + timeout
    ctx = GLib.MainContext.default()
    while time.monotonic() < end:
        while ctx.iteration(False):
            pass
        if pred():
            return
        time.sleep(0.02)
    raise SystemExit(f"FAIL: {what} within {timeout} s")


def pump(seconds):
    end = time.monotonic() + seconds
    ctx = GLib.MainContext.default()
    while time.monotonic() < end:
        while ctx.iteration(False):
            pass
        time.sleep(0.02)


def has_name(bus, name):
    names = bus.call_sync(
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "ListNames",
        None,
        GLib.VariantType("(as)"),
        Gio.DBusCallFlags.NONE,
        -1,
        None,
    ).unpack()[0]
    return name in names


def register(bus, info):
    """One listener on Console_0: its peer connection, serving the listener interfaces."""
    ours, theirs = socket.socketpair()
    stream = Gio.Socket.connection_factory_create_connection(Gio.Socket.new_from_fd(ours.detach()))
    peer = []

    def call(conn, sender, path, iface, method, params, inv):
        inv.return_value(None)

    def prop(conn, sender, path, iface, name):
        return GLib.Variant("as", IFACES) if name == "Interfaces" else None

    def ready(src, res):
        conn = Gio.DBusConnection.new_finish(res)
        for name in IFACES:
            conn.register_object(LISTENER, info.lookup_interface(name), call, prop, None)
        peer.append(conn)

    Gio.DBusConnection.new(
        stream, None, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT, None, None, ready
    )
    fds = Gio.UnixFDList.new()
    idx = fds.append(theirs.fileno())
    theirs.close()
    done = []
    bus.call_with_unix_fd_list(
        "org.qemu",
        "/org/qemu/Display1/Console_0",
        "org.qemu.Display1.Console",
        "RegisterListener",
        GLib.Variant("(h)", (idx,)),
        None,
        Gio.DBusCallFlags.NONE,
        10000,
        fds,
        None,
        lambda src, res: done.append(src.call_with_unix_fd_list_finish(res)),
    )
    wait_for(lambda: peer and done, 10, "RegisterListener")
    return peer[0]


def main():
    qemu, xml = sys.argv[1:3]
    if not shutil.which("dbus-daemon"):
        print("SKIP: no dbus-daemon")
        return 77
    info = Gio.DBusNodeInfo.new_for_xml(open(xml).read())
    tmp = tempfile.mkdtemp(prefix="check-qemu-listener-")
    addr = f"unix:path={tmp}/bus.sock"
    log_path = os.path.join(tmp, "qemu.out")
    bus_proc = subprocess.Popen(["dbus-daemon", "--session", f"--address={addr}", "--nofork"])
    qemu_proc = None
    try:
        wait_for(lambda: os.path.exists(f"{tmp}/bus.sock"), 10, "the private bus")
        bus = Gio.DBusConnection.new_for_address_sync(
            addr,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
            | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        with open(log_path, "wb") as log:
            qemu_proc = subprocess.Popen(
                [
                    qemu,
                    "-M",
                    "virt",
                    "-S",
                    "-nodefaults",
                    "-display",
                    f"dbus,addr={addr}",
                    "-device",
                    "virtio-gpu-pci",
                    "-trace",
                    "displaychangelistener_*",
                    "-trace",
                    "dbus_listener_vanished",
                ],
                stdin=subprocess.DEVNULL,
                stdout=log,
                stderr=subprocess.STDOUT,
            )
        wait_for(
            lambda: has_name(bus, "org.qemu") or qemu_proc.poll() is not None, 30, "QEMU's display"
        )
        if qemu_proc.poll() is not None:
            print(open(log_path).read())
            raise SystemExit("FAIL: QEMU exited")
        for i in range(CYCLES):
            peer = register(bus, info)
            pump(0.3)  # the listener answers QEMU's first calls
            peer.close_sync(None)
            vanished = i + 1
            wait_for(
                lambda: open(log_path).read().count("dbus_listener_vanished") >= vanished,
                10,
                f"QEMU seeing listener {i} vanish",
            )
        pump(0.5)
    finally:
        if qemu_proc:
            qemu_proc.terminate()
            qemu_proc.wait(10)
        bus_proc.terminate()
        bus_proc.wait(10)
    out = open(log_path).read()
    shutil.rmtree(tmp)
    registered = re.findall(r"displaychangelistener_register (0x[0-9a-f]+) \[ dbus \]", out)
    unregistered = set(
        re.findall(r"displaychangelistener_unregister (0x[0-9a-f]+) \[ dbus \]", out)
    )
    # QEMU's exit (SIGTERM) unregisters nothing, so every unregister seen came from a close
    fails = []
    if len(registered) != CYCLES:
        fails.append(f"{len(registered)} listeners registered, not {CYCLES}")
    leaked = [r for r in registered if r not in unregistered]
    if leaked:
        fails.append(f"listeners still registered after their connection closed: {leaked}")
    if "Failed to call" in out:
        fails.append("QEMU reported a failed call")
    if fails:
        print(out)
        for f in fails:
            print("FAIL:", f)
        return 1
    print(f"ok: {CYCLES} listeners unregistered when their connection closed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
