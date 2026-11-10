# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compact VM overview, with secondary file and process details folded away."""

from gi.repository import Gtk, Pango


class Summary(Gtk.Box):
    def __init__(self, manager):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        self.manager = manager
        self.tag_style = Gtk.CssProvider()
        self.tag_style.load_from_data(b"""
            .beanjar-layer-tag {
                padding: 2px 8px;
                border-radius: 999px;
                background-color: alpha(currentColor, 0.08);
            }
        """)
        Gtk.StyleContext.add_provider_for_display(
            self.get_display(), self.tag_style, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
        self.values = {}
        self.fields = {}
        banner = Gtk.Box(spacing=12)
        self.state = Gtk.Label(xalign=0)
        self.state.add_css_class("heading")
        self.platform = Gtk.Label(xalign=0, hexpand=True, ellipsize=Pango.EllipsizeMode.END)
        banner.append(self.state)
        banner.append(self.platform)
        self.append(banner)
        self.firmware = Gtk.Label(
            xalign=0, selectable=True, wrap=True, wrap_mode=Pango.WrapMode.WORD_CHAR
        )
        self.firmware.add_css_class("dim-label")
        self.append(self.firmware)
        layers = Gtk.Box(spacing=12)
        layers.append(Gtk.Label(label="Layers", xalign=0))
        self.layers = Gtk.FlowBox(
            selection_mode=Gtk.SelectionMode.NONE,
            homogeneous=False,
            min_children_per_line=1,
            max_children_per_line=12,
            column_spacing=6,
            row_spacing=6,
            hexpand=True,
            halign=Gtk.Align.START,
        )
        layers.append(self.layers)
        self.append(layers)
        self.cards = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        self.append(self.cards)
        hardware = self.card("Hardware")
        for key, title in (
            ("smp", "CPUs"),
            ("mem", "Memory"),
            ("gpu", "GPU"),
            ("display", "Display"),
            ("audio", "Audio"),
            ("free_page_reporting", "Free page reporting"),
        ):
            hardware.append(self.field(key, title))
        identity = self.card("Identity")
        for key, title in (("mac", "MAC"), ("serial", "Serial")):
            identity.append(self.field(key, title, copy=True))
        identity.append(self.field("model_key", "Model key"))
        network = self.card("Network")
        self.network_rows = {}
        for key, title in (
            ("network", "Connection mode"),
            ("address", "Host address"),
            ("lan_address", "LAN address"),
            ("listen", "Service ports on"),
        ):
            row = self.field(key, title)
            network.append(row)
            self.network_rows[key] = row
        self.inspector_slot = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.append(self.inspector_slot)
        self.runtime = Gtk.Expander(label="Files and runtime details")
        files = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, margin_top=10)
        for key, title in (
            ("directory", "VM directory"),
            ("firmware_path", "Firmware location"),
            ("qemu_pid", "QEMU PID"),
            ("viewer_pid", "Viewer PID"),
        ):
            files.append(self.field(key, title, wrap=True))
        self.runtime.set_child(files)
        self.append(self.runtime)
        buttons = Gtk.Box(spacing=8)
        buttons.append(Gtk.Button(label="Open VM Folder", action_name="win.folder"))
        buttons.append(Gtk.Button(label="Open Log Folder", action_name="win.logs"))
        self.append(buttons)

    @staticmethod
    def flow():
        return Gtk.FlowBox(
            selection_mode=Gtk.SelectionMode.NONE,
            homogeneous=False,
            min_children_per_line=1,
            max_children_per_line=2,
            column_spacing=12,
            row_spacing=12,
        )

    def card(self, title):
        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        card.add_css_class("card")
        card.set_vexpand(False)
        content = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL,
            spacing=10,
            margin_top=12,
            margin_bottom=12,
            margin_start=12,
            margin_end=12,
        )
        heading = Gtk.Label(label=title, xalign=0)
        heading.add_css_class("heading")
        content.append(heading)
        card.append(content)
        self.cards.append(card)
        fields = self.flow()
        content.append(fields)
        return fields

    def field(self, key, title, copy=False, wrap=False):
        row = Gtk.Box(spacing=10, hexpand=True)
        if not wrap:
            row.set_size_request(260, -1)
        alignment = Gtk.Align.START if wrap else Gtk.Align.CENTER
        label = Gtk.Label(label=title, xalign=0, valign=alignment)
        if copy:
            label.set_width_chars(6)
        label.add_css_class("dim-label")
        value = Gtk.Label(
            xalign=0 if wrap or copy else 1,
            valign=alignment,
            hexpand=True,
            selectable=True,
            ellipsize=Pango.EllipsizeMode.NONE if wrap else Pango.EllipsizeMode.END,
            wrap=wrap,
            wrap_mode=Pango.WrapMode.WORD_CHAR,
            max_width_chars=24,
        )
        row.append(label)
        row.append(value)
        self.fields[key] = value
        if copy:
            button = Gtk.Button(icon_name="edit-copy-symbolic", tooltip_text=f"Copy {title}")
            button.add_css_class("flat")
            button.connect("clicked", lambda *_: self.copy(key, title))
            row.append(button)
        return row

    def copy(self, key, title):
        value = self.values.get(key)
        if value:
            self.get_clipboard().set(value)
            self.manager.say(f"{title} copied")

    def update(self, values, state="", platform="", firmware="", layers=()):
        self.values = values
        self.state.set_text(state)
        self.platform.set_text(platform)
        self.firmware.set_text(firmware)
        self.firmware.set_tooltip_text(values.get("fw") or None)
        for key, label in self.fields.items():
            label.set_text(values.get(key) or "—")
            label.set_tooltip_text(values.get(key) or None)
        link = values.get("link", "nat")
        self.network_rows["address"].get_parent().set_visible(link == "nat")
        self.network_rows["lan_address"].get_parent().set_visible(link == "lan")
        self.network_rows["listen"].get_parent().set_visible(link != "off")
        layer_names = tuple(layers)
        if getattr(self, "layer_names", None) != layer_names:
            self.layer_names = layer_names
            while child := self.layers.get_first_child():
                self.layers.remove(child)
            for name in layer_names:
                chip = Gtk.Label(label=name)
                chip.add_css_class("beanjar-layer-tag")
                self.layers.append(chip)
