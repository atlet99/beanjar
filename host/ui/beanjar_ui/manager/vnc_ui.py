# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The selected VM's VNC server, a group of its Summary beside the traffic inspector, shown only while
`beanjar vnc` serves it (VM.vnc_info): its address, with link lan the VM's LAN address and "On this
host" for this host's way in (its own address), Copy Address and its password (always one, random
per start unless BEANJAR_VNC_PASSWORD chose it, in the owner-only run/vnc.password) with Copy
Password."""

from gi.repository import Gtk, Pango

from beanjar import inspector

DESCRIPTION = (
    "beanjar vnc serves this VM's screen to VNC clients, behind its own password (RSA-AES)."
)


class Panel(Gtk.Box):
    def __init__(self, manager):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.add_css_class("card")
        self.manager = manager
        self.info = None
        self.secret = ""
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
        title = Gtk.Label(label="VNC", xalign=0)
        title.add_css_class("heading")
        self.status = Gtk.Label(xalign=1, hexpand=True, ellipsize=Pango.EllipsizeMode.END)
        heading.append(title)
        heading.append(self.status)
        content.append(heading)
        description = Gtk.Label(
            label=DESCRIPTION, xalign=0, wrap=True, wrap_mode=Pango.WrapMode.WORD_CHAR
        )
        description.add_css_class("dim-label")
        self.details = Gtk.Expander(label="Connection details and credentials")
        details = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, margin_top=10)
        self.details.set_child(details)
        details.append(description)
        buttons = Gtk.Box(spacing=8)
        self.address = Gtk.Label(
            xalign=0,
            hexpand=True,
            selectable=True,
            ellipsize=Pango.EllipsizeMode.END,
            max_width_chars=32,
        )
        self.address.add_css_class("dim-label")
        self.copy = Gtk.Button(label="Copy Address")
        self.copy.connect("clicked", self.copy_address)
        buttons.append(self.address)
        buttons.append(self.copy)
        content.append(buttons)
        # link lan: this host's way in (the address above is the LAN's, which the host cannot reach)
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
        credentials = Gtk.Box(spacing=8)
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
            credentials.append(widget)
        details.append(credentials)
        content.append(self.details)
        self.set_visible(False)

    def show_info(self, info):
        """info: VM.vnc_info(), or None (hidden)."""
        self.info = info
        self.set_visible(bool(info))
        if not info:
            self.secret = ""
            return
        self.secret = inspector.read_password(info.get("password_file"))
        self.status.set_text(f"Running (pid {info['pid']})")
        self.address.set_text(info["url"])
        self.address.set_tooltip_text(info["url"])
        self.local_row.set_visible(bool(info.get("local_url")))
        self.local_url.set_text(info.get("local_url") or "")
        self.password.set_text(self.secret or "Password unavailable")
        self.password.set_tooltip_text(info.get("password_file"))
        self.copy_password.set_sensitive(bool(self.secret))

    def refresh(self, vm):
        """Follow vm, the selected VM (None: none, or one that does not read)."""
        info = vm.vnc_info() if vm is not None else None
        if info != self.info:
            self.show_info(info)

    def copy_address(self, *_):
        if self.info:
            self.get_clipboard().set(self.info["url"])
            self.manager.say("VNC address copied")

    def copy_secret(self, *_):
        if self.secret:
            self.get_clipboard().set(self.secret)
            self.manager.say("VNC password copied")
