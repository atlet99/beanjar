# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The selected VM's traffic inspector, a group of its Summary: its own mitmweb (each VM with mode
mitm has one, on its own port and with its own password; the CA it signs with is the library's),
with its status, URL, Open in Browser (logged in), Copy URL and Copy Password, and with link lan
the VM's leased LAN address. The full mitmweb UI opens in the system browser. With link lan the URL
is the LAN one, for other machines; this host cannot reach its own macvlan children, so its
browser opens the inspector at the VM's own address, shown as "On this host"."""

import os
import threading
import time

from gi.repository import Gio, GLib, Gtk, Pango

from beanjar import core, inspector, mitmsetup, paths, vmsettings

DESCRIPTION = (
    "This VM's own mitmweb, on its own port and with its own password, while it runs. "
    "The CA certificate it signs with is the library's, shared by all of its VMs."
)


class Panel(Gtk.Box):
    def __init__(self, manager):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.add_css_class("card")
        self.manager = manager
        self.key = None
        self.generation = 0
        self.next_check = 0
        self.pending = False
        self.closed = False
        self.vm_name = None
        self.state = inspector.State("mitm", "Not running")
        content = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL,
            spacing=10,
            margin_top=12,
            margin_bottom=12,
            margin_start=12,
            margin_end=12,
        )
        self.append(content)
        heading = Gtk.Box(spacing=8)
        title = Gtk.Label(label="Traffic Inspector", xalign=0)
        title.add_css_class("heading")
        self.status = Gtk.Label(xalign=1, hexpand=True, ellipsize=Pango.EllipsizeMode.END)
        self.installation = Gtk.Label()
        self.installation.add_css_class("dim-label")
        self.install = Gtk.Button(label="Install…")
        self.install.connect("clicked", lambda *_: manager.install_mitm())
        for widget in (title, self.status):
            heading.append(widget)
        content.append(heading)
        self.description = Gtk.Label(
            label=DESCRIPTION, xalign=0, wrap=True, wrap_mode=Pango.WrapMode.WORD_CHAR
        )
        self.description.add_css_class("dim-label")
        self.details = Gtk.Expander(label="Connection details and credentials")
        details = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, margin_top=10)
        self.details.set_child(details)
        details.append(self.description)
        # link lan: the address the VM leased on the LAN (its ports, and the inspector's, are there)
        self.lan_row = Gtk.Box(spacing=8)
        label = Gtk.Label(label="LAN address", xalign=0)
        label.add_css_class("dim-label")
        self.lan_address = Gtk.Label(
            xalign=0,
            hexpand=True,
            selectable=True,
            ellipsize=Pango.EllipsizeMode.END,
            max_width_chars=32,
        )
        self.lan_row.append(label)
        self.lan_row.append(self.lan_address)
        details.append(self.lan_row)
        self.buttons = Gtk.Box(spacing=8)
        self.address = Gtk.Label(
            xalign=0,
            hexpand=True,
            selectable=True,
            ellipsize=Pango.EllipsizeMode.END,
            max_width_chars=32,
        )
        self.address.add_css_class("dim-label")
        self.open = Gtk.Button(label="Open in Browser")
        self.open.add_css_class("suggested-action")
        self.open.connect("clicked", self.open_browser)
        self.copy = Gtk.Button(label="Copy URL")
        self.copy.connect("clicked", self.copy_url)
        content.append(self.address)
        for widget in (self.open, self.copy):
            self.buttons.append(widget)
        content.append(self.buttons)
        from .summary_ui import Summary

        self.limits = Summary.flow()
        content.append(self.limits)
        content.append(self.details)
        # link lan: the URL that works from this host (the one above is the LAN's), which Open uses
        self.local_row = Gtk.Box(spacing=8)
        label = Gtk.Label(label="On this host", xalign=0)
        label.add_css_class("dim-label")
        self.local_url = Gtk.Label(
            xalign=0,
            hexpand=True,
            selectable=True,
            ellipsize=Pango.EllipsizeMode.END,
            max_width_chars=32,
        )
        self.local_row.append(label)
        self.local_row.append(self.local_url)
        details.append(self.local_row)
        # the inspector's password (netns.py makes one per start); Open in Browser logs in with it
        self.credentials = Gtk.Box(spacing=8)
        label = Gtk.Label(label="Password", xalign=0)
        label.add_css_class("dim-label")
        self.password = Gtk.Label(
            xalign=0,
            hexpand=True,
            selectable=True,
            ellipsize=Pango.EllipsizeMode.END,
            max_width_chars=32,
        )
        self.password.add_css_class("monospace")
        self.copy_password = Gtk.Button(label="Copy Password")
        self.copy_password.connect("clicked", self.copy_secret)
        for widget in (label, self.password, self.copy_password):
            self.credentials.append(widget)
        details.append(self.credentials)
        setup = Gtk.Box(spacing=8)
        self.installation.set_hexpand(True)
        self.installation.set_xalign(0)
        setup.append(self.installation)
        setup.append(self.install)
        details.append(setup)
        self.log = Gtk.TextView(
            editable=False,
            cursor_visible=False,
            monospace=True,
            wrap_mode=Gtk.WrapMode.CHAR,
            top_margin=8,
            bottom_margin=8,
            left_margin=8,
            right_margin=8,
        )
        self.scroll = Gtk.ScrolledWindow(min_content_height=100, max_content_height=100)
        self.scroll.set_child(self.log)
        self.diagnostics = Gtk.Expander(label="Proxy diagnostics")
        self.diagnostics.set_child(self.scroll)
        content.append(self.diagnostics)

    def show_state(self, state):
        self.state = state
        self.status.set_text(state.status)
        self.status.set_tooltip_text(state.status)
        running = state.status != "Not running"
        self.address.set_text(
            state.url or ("Not running" if not running else "Inspector unavailable")
        )
        self.address.set_tooltip_text(state.url or None)
        self.local_row.set_visible(bool(state.local_url))
        self.local_url.set_text(state.local_url)
        self.open.set_sensitive(state.ready)
        self.open.set_tooltip_text(state.here_url or None)
        self.copy.set_sensitive(bool(state.url))
        self.password.set_text(state.password or ("Password unavailable" if state.url else "—"))
        self.password.set_tooltip_text(state.password_file or None)
        self.copy_password.set_sensitive(bool(state.password))

    def refresh(self, vm):
        """Follow vm, the selected VM (None: none, or one that does not read): shown only while
        its network is mitm, with its endpoint while it runs."""
        try:
            mitm_vm = (
                vm is not None
                and vmsettings.guest_net(vm.config.get("link"), vm.config.get("mode")) == "mitm"
            )
        except core.Error:
            mitm_vm = False
        self.set_visible(mitm_vm)
        if not mitm_vm:
            vm = None
        lan = vm is not None and vm.config.get("link") == "lan"
        self.lan_row.set_visible(lan)
        if lan:
            self.lan_address.set_text(vm.lan_address() or "Not running")
        state = inspector.snapshot(vm)
        key = (vm.name if vm else None, vm.pid() if vm else None, state)
        now = time.monotonic()
        if key != self.key:
            self.key = key
            self.generation += 1
            self.next_check = 0
            self.show_state(state)
        mitm = state.mode == "mitm"
        if now < self.next_check:
            return
        self.next_check = now + 5
        installed = (
            mitmsetup.available(paths.TREE)
            if paths.TREE.installed
            else os.access(paths.TREE.mitmweb(), os.X_OK)
        )
        self.installation.set_text(
            "mitmproxy installed" if installed else "mitmproxy not installed"
        )
        self.install.set_visible(paths.TREE.installed and not installed)
        self.install.set_sensitive(None not in self.manager.ops)
        text = inspector.log_tail(vm)
        text = text or "No proxy diagnostics yet."
        buffer = self.log.get_buffer()
        if buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), False) != text:
            buffer.set_text(text)
        if mitm and state.url and not self.pending:
            self.pending = True
            generation = self.generation

            def worker():
                result = inspector.probe(state)
                GLib.idle_add(self.checked, generation, result)

            threading.Thread(target=worker, daemon=True).start()

    def checked(self, generation, state):
        self.pending = False
        if not self.closed and generation == self.generation:
            self.show_state(state)
        return False

    def open_browser(self, *_):
        if not self.state.ready:
            return
        try:
            # the browser runs on this host, which reaches a link lan VM only at its local_url
            Gio.AppInfo.launch_default_for_uri(self.state.here_login_url, None)
        except GLib.Error as exc:
            self.manager.say(f"Could not open inspector: {exc.message}")

    def copy_url(self, *_):
        # the plain address: a pasted link does not carry the password
        if self.state.url:
            self.get_clipboard().set(self.state.url)
            self.manager.say("Inspector URL copied")

    def copy_secret(self, *_):
        if self.state.password:
            self.get_clipboard().set(self.state.password)
            self.manager.say("Inspector password copied")
