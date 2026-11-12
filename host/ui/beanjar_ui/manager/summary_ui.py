# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compact VM overview, with secondary file and process details folded away. The state line has
why a running VM runs (its reason) and the VM's tags beside its layers; the Hardware, Identity and
Network cards each have an Edit that opens Settings at their page (win.settings-page, insensitive
while the VM runs), and the VM's and its logs' folders open from their rows."""

import datetime

from gi.repository import GLib, GObject, Gtk, Pango


def reason_note(reason, now=None):
    """When and by whom a reason was set: "set 10:58 by user" (today), "set 2026-11-11 10:58 by
    user, agent" (before)."""
    try:
        when = datetime.datetime.strptime(reason.get("set") or "", "%Y-%m-%dT%H:%M:%S%z")
        now = now or datetime.datetime.now(when.tzinfo)
        when = when.astimezone(now.tzinfo)
        stamp = when.strftime("%H:%M" if when.date() == now.date() else "%Y-%m-%d %H:%M")
    except ValueError:
        stamp = reason.get("set") or "?"
    who = ", ".join(filter(None, (reason.get("user"), reason.get("by"))))
    return f"set {stamp}" + (f" by {who}" if who else "")


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
            .beanjar-user-tag {
                padding: 2px 8px;
                border-radius: 999px;
                background-color: alpha(@accent_bg_color, 0.14);
                color: @accent_color;
            }
            .beanjar-reason {
                color: @warning_color;
            }
        """)
        Gtk.StyleContext.add_provider_for_display(
            self.get_display(), self.tag_style, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
        self.values = {}
        self.fields = {}
        # the state line: state and platform, then why it runs (beside them; under them, narrow)
        self.headline = Gtk.Box(spacing=12)
        banner = Gtk.Box(spacing=12, hexpand=True)
        self.state = Gtk.Label(xalign=0)
        self.state.add_css_class("heading")
        self.platform = Gtk.Label(xalign=0, hexpand=True, ellipsize=Pango.EllipsizeMode.END)
        banner.append(self.state)
        banner.append(self.platform)
        self.headline.append(banner)
        self.reason = Gtk.Box(spacing=6, visible=False)
        pin = Gtk.Image(icon_name="view-pin-symbolic")
        pin.add_css_class("beanjar-reason")
        self.reason_text = Gtk.Label(
            xalign=0, selectable=True, wrap=True, wrap_mode=Pango.WrapMode.WORD_CHAR
        )
        self.reason_text.add_css_class("beanjar-reason")
        self.reason_set = Gtk.Label(xalign=0)
        self.reason_set.add_css_class("dim-label")
        for widget in (pin, self.reason_text, self.reason_set):
            self.reason.append(widget)
        self.headline.append(self.reason)
        self.append(self.headline)
        self.firmware = Gtk.Label(
            xalign=0, selectable=True, wrap=True, wrap_mode=Pango.WrapMode.WORD_CHAR
        )
        self.firmware.add_css_class("dim-label")
        self.append(self.firmware)
        # the layers, and the VM's tags beside them (under them, narrow)
        self.chips = Gtk.Box(spacing=24)
        layers, self.layers = self.chip_group("Layers", True)
        self.tags_group, self.tags = self.chip_group("Tags", False)
        self.tags_group.set_visible(False)
        self.chips.append(layers)
        self.chips.append(self.tags_group)
        self.append(self.chips)
        self.cards = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        self.append(self.cards)
        self.edits = []
        hardware = self.card("Hardware", "hardware")
        for key, title in (
            ("smp", "CPUs"),
            ("mem", "Memory"),
            ("gpu", "GPU"),
            ("display", "Display"),
            ("audio", "Audio"),
            ("free_page_reporting", "Free page reporting"),
        ):
            hardware.append(self.field(key, title))
        identity = self.card("Identity", "identity")
        for key, title in (("mac", "MAC"), ("serial", "Serial")):
            identity.append(self.field(key, title, copy=True))
        identity.append(self.field("model_key", "Model key"))
        network = self.card("Network", "network")
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
        for key, title, action, tooltip in (
            ("directory", "VM directory", "win.folder", "Open VM Folder"),
            ("logs", "Logs", "win.logs", "Open Log Folder"),
            ("firmware_path", "Firmware location", None, None),
            ("qemu_pid", "QEMU PID", None, None),
            ("viewer_pid", "Viewer PID", None, None),
        ):
            row = self.field(key, title, wrap=True, title_chars=16)
            if action:
                button = Gtk.Button(
                    label="Open", action_name=action, tooltip_text=tooltip, valign=Gtk.Align.CENTER
                )
                button.add_css_class("flat")
                row.append(button)
                # centred with the button (a taller line than the text), not on the first line
                self.fields[key].set_valign(Gtk.Align.CENTER)
                row.get_first_child().set_valign(Gtk.Align.CENTER)
            files.append(row)
        self.runtime.set_child(files)
        self.append(self.runtime)

    def follow(self, breakpoint):
        """Narrow (breakpoint, the window's): the reason under the state line, the tags under the
        layers."""
        vertical = GObject.Value(Gtk.Orientation, Gtk.Orientation.VERTICAL)
        for box in (self.headline, self.chips):
            breakpoint.add_setter(box, "orientation", vertical)
        breakpoint.add_setter(self.chips, "spacing", GObject.Value(int, 8))

    def chip_group(self, title, expand):
        group = Gtk.Box(spacing=12, hexpand=expand)
        group.append(Gtk.Label(label=title, xalign=0, valign=Gtk.Align.CENTER))
        flow = Gtk.FlowBox(
            selection_mode=Gtk.SelectionMode.NONE,
            homogeneous=False,
            min_children_per_line=1,
            max_children_per_line=12,
            column_spacing=6,
            row_spacing=6,
            hexpand=expand,
            halign=Gtk.Align.START,
        )
        group.append(flow)
        return group, flow

    @staticmethod
    def set_chips(flow, names, style):
        while child := flow.get_first_child():
            flow.remove(child)
        for name in names:
            chip = Gtk.Label(label=name, halign=Gtk.Align.START)
            chip.add_css_class(style)
            flow.append(chip)

    def set_edit_tooltip(self, text):
        for edit in self.edits:
            edit.set_tooltip_text(text)

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

    def card(self, title, page=None):
        """A card with a heading (and with page, an Edit for Settings at that page); its fields'
        flow box."""
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
        head = Gtk.Box(spacing=8)
        heading = Gtk.Label(label=title, xalign=0, hexpand=True)
        heading.add_css_class("heading")
        head.append(heading)
        if page:
            edit = Gtk.Button(
                label="Edit",
                valign=Gtk.Align.CENTER,
                action_name="win.settings-page",
                action_target=GLib.Variant.new_string(page),
            )
            edit.add_css_class("flat")
            head.append(edit)
            self.edits.append(edit)
        content.append(head)
        card.append(content)
        self.cards.append(card)
        fields = self.flow()
        content.append(fields)
        return fields

    def field(self, key, title, copy=False, wrap=False, title_chars=None):
        row = Gtk.Box(spacing=10, hexpand=True)
        if not wrap:
            row.set_size_request(260, -1)
        alignment = Gtk.Align.START if wrap else Gtk.Align.CENTER
        label = Gtk.Label(label=title, xalign=0, valign=alignment)
        if copy:
            label.set_width_chars(6)
        if title_chars:
            label.set_width_chars(title_chars)
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

    def update(
        self, values, state="", platform="", firmware="", layers=(), reason=None, tags=(), full=None
    ):
        """Show values (key: text), the state line and the chips; reason: why the VM runs
        (VM.reason: text, set, user, by), None if it has none; full: the values whose tooltip
        and copy are not the text shown (key: text, e.g. a path shown with ~)."""
        full = full or {}
        self.values = {**values, **full}
        self.state.set_text(state)
        self.platform.set_text(platform)
        self.firmware.set_text(firmware)
        self.firmware.set_tooltip_text(values.get("fw") or None)
        for key, label in self.fields.items():
            label.set_text(values.get(key) or "—")
            label.set_tooltip_text(full.get(key) or values.get(key) or None)
        link = values.get("link", "nat")
        self.network_rows["address"].get_parent().set_visible(link == "nat")
        self.network_rows["lan_address"].get_parent().set_visible(link == "lan")
        self.network_rows["listen"].get_parent().set_visible(link != "off")
        layer_names = tuple(layers)
        if getattr(self, "layer_names", None) != layer_names:
            self.layer_names = layer_names
            self.set_chips(self.layers, layer_names, "beanjar-layer-tag")
        tag_names = tuple(tags)
        if getattr(self, "tag_names", None) != tag_names:
            self.tag_names = tag_names
            self.set_chips(self.tags, tag_names, "beanjar-user-tag")
            self.tags_group.set_visible(bool(tag_names))
        self.reason.set_visible(bool(reason))
        if reason:
            self.reason_text.set_text(reason["text"])
            self.reason_set.set_text(reason_note(reason))
            self.reason.set_tooltip_text(
                "Why it runs (beanjar reason): Shut Down asks before overriding it"
            )
