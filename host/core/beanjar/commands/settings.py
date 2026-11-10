# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Settings: a VM's (settings), the library's (library config) and this machine's (host config,
add, remove, list, prune-layer-images)."""

import argparse
import os
import sys

from .. import capacity, core, hostconfig, paths, remote, vmsettings
from ..core import Error
from .common import Usage, emit, layer_versions, progress, table, text_value


def settings_of(a):
    """create's settings (vm.toml's but fw) from its options."""
    keys = ("mem", "smp", "layers", "link", "mode", "display", "audio")
    return {**{k: getattr(a, k) for k in keys}, "tags": a.tag}


def settings_view(vm):
    """The VM's settings (SETTINGS' keys): vm.toml's values, None where it leaves the default; and,
    with a versioned layer, layer_versions: {layer: the version it runs} (plain NAME's default
    named), which is no setting of its own."""
    c = vm.config
    out = {k: c.get(k) for k in vmsettings.SETTINGS}
    versions = layer_versions(c.get("layers"))
    if versions:
        out["layer_versions"] = versions
    return out


def key_values(items, keys, cmd):
    """KEY=VALUE words as {KEY: VALUE}, each KEY one of keys."""
    values = {}
    for kv in items:
        k, sep, v = kv.partition("=")
        if not sep or k not in keys:
            raise Usage(f"{kv}: KEY=VALUE with KEY one of {' '.join(keys)} (beanjar {cmd} --help)")
        values[k] = v
    return values


def set_or_unset(a, cmd, keys, unsettable, apply):
    """A settings command's verb: show (none), set KEY=VALUE... or unset KEY...; apply(values) makes
    the change, an unset being KEY's empty value, which only the keys of unsettable take (set refuses
    it for them, naming unset)."""
    verb = a.verb or "show"
    if verb == "show":
        if a.items:
            raise Usage(f"beanjar {cmd} show takes no KEY")
        return
    if not a.items:
        raise Usage(
            f"beanjar {cmd} {verb} needs " + ("KEY=VALUE..." if verb == "set" else "KEY...")
        )
    if verb == "set":
        values = key_values(a.items, keys, cmd)
        for k, v in values.items():
            if k in unsettable and not v.strip():
                raise Usage(f"{k}= is empty: beanjar {cmd} unset {k} sets it back to the default")
    else:
        for k in a.items:
            if k not in keys:
                raise Usage(f"{k}: KEY one of {' '.join(keys)} (beanjar {cmd} --help)")
            if k not in unsettable:
                raise Usage(
                    f"{k} has no unset (only {', '.join(unsettable)}); set it to a value instead"
                )
        values = dict.fromkeys(a.items, "")
    apply(values)


def cmd_settings(lib, a):
    """A VM's settings: shown, set (while it is off) or unset (back to the default)."""
    unsettable = [k for k, h in vmsettings.SETTING_HELP.items() if h[3]]
    changed = []
    set_or_unset(
        a,
        f"settings {a.name}",
        tuple(vmsettings.SETTINGS),
        unsettable,
        lambda values: changed.append(lib.configure(a.name, out=progress, **values)),
    )
    vm = changed[0] if changed else lib.vm(a.name)
    emit(a, settings_view(vm), none="(default)")
    if changed and (a.verb or "show") == "set":
        offer_mitm(vm, a)
    return 0


def settings_help():
    """The settings' keys as `beanjar settings --help` lists them, from vmsettings.SETTING_HELP."""
    import textwrap

    lines = [
        "Settings change only while the VM is off. Each KEY, its values, its default, and what",
        "`unset` brings back where it has one:",
        "",
    ]
    for k, (values, default, what, unset) in vmsettings.SETTING_HELP.items():
        lines.append(f"  {k}={values}")
        lines.append(
            textwrap.fill(
                f"{what}; default {default}" + (f"; unset: {unset}" if unset else ""),
                width=94,
                initial_indent="      ",
                subsequent_indent="      ",
            )
        )
    return "\n".join(lines)


def offer_mitm(vm, a):
    from .. import paths, mitmsetup

    if (
        paths.TREE.installed
        and vmsettings.guest_net(vm.config.get("link"), vm.config.get("mode")) == "mitm"
        and not mitmsetup.available(paths.TREE)
    ):
        if sys.stdin.isatty() and not a.json:
            try:
                mitmsetup.ensure(paths.TREE)
            except mitmsetup.Error as exc:
                progress(str(exc))
        else:
            progress(
                "MITM setup pending: run beanjar mitm setup --install before starting this VM."
            )


def config_command(a, cmd, keys, unsettable, setter):
    set_or_unset(a, cmd, keys, unsettable, lambda values: [setter(k, v) for k, v in values.items()])


def cmd_library_config(lib, a):
    """The library's settings (config.toml): shown, set or unset."""
    config_command(a, "library config", vmsettings.CONFIG_KEYS, ("lan_parent",), lib.set_option)
    shown = dict(lib.config, mac_prefix=lib.mac_prefix, addresses=lib.addresses)
    return emit(a, {k: shown[k] for k in vmsettings.CONFIG_KEYS if k in shown})


def cmd_host_config(lib, a):
    """This machine's settings, every library's ($XDG_CONFIG_HOME/beanjar/config.toml)."""
    config_command(
        a,
        "host config",
        hostconfig.GLOBAL_KEYS,
        hostconfig.GLOBAL_KEYS,
        hostconfig.set_global_option,
    )
    shown = hostconfig.global_config()
    data = {
        "max_running": shown.get("max_running"),
        "firmware_dirs": shown.get("firmware_dirs") or list(paths.DEFAULT_FIRMWARE_DIRS),
        "memory_headroom_mib": shown.get("memory_headroom_mib") or capacity.START_HEADROOM_MIB,
    }
    text = "\n".join(
        [
            f"max_running: {text_value(data['max_running'], '(not set: no limit)')}",
            f"firmware_dirs: {':'.join(data['firmware_dirs'])}"
            + ("" if "firmware_dirs" in shown else " (not set: the default)"),
            f"memory_headroom_mib: {data['memory_headroom_mib']}"
            + ("" if "memory_headroom_mib" in shown else " (not set: the default)"),
        ]
    )
    return emit(a, data, text)


def cmd_host_add(lib, a):
    remote.add_host(a.host_name, a.uri)
    return emit(a, {"name": a.host_name, "uri": a.uri}, f"added host {a.host_name} ({a.uri})")


def cmd_host_remove(lib, a):
    remote.remove_host(a.host_name)
    return emit(a, {"name": a.host_name, "removed": True}, f"removed host {a.host_name}")


def cmd_host_list(lib, a):
    rows = [{"name": n, "uri": u} for n, u in remote.hosts().items()]
    if a.json or not rows:
        return emit(a, rows, "no hosts (beanjar host add NAME ssh://[USER@]HOST[:PORT])")
    return emit(
        a,
        rows,
        table(rows, [("NAME", lambda r: r["name"], False), ("URI", lambda r: r["uri"], False)]),
    )


def cmd_host_prune_layer_images(lib, a):
    """Remove the cached layer images no VM of this library (and its development libraries) uses."""
    from .. import base, layerimage, paths

    libraries = [core.Library(r) for r in base.slot_domain(lib.root)[1] if os.path.isdir(r)]
    try:
        removed = layerimage.prune(
            paths.TREE,
            lambda: layerimage.referenced(paths.TREE, libraries),
            dry_run=a.dry_run,
        )
    except layerimage.Error as exc:
        raise Error(str(exc)) from None
    verb = "would remove" if a.dry_run else "removed"
    return emit(
        a,
        {"removed": removed, "dry_run": a.dry_run},
        "\n".join([f"{verb} {len(removed)} unused layer image(s)"] + removed),
    )


def register_vm_settings(cli):
    sub, command, vm_name = cli.sub, cli.command, cli.vm_name
    s = command(
        sub,
        "settings",
        "a VM's settings (vm.toml): show them, set KEY=VALUE or unset KEY (back to the "
        "default) while it is off",
        cmd_settings,
        usage="beanjar settings NAME [show | set KEY=VALUE... | unset KEY...] [--json]",
        epilog=settings_help(),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    vm_name(s)
    s.add_argument(
        "verb",
        nargs="?",
        choices=("show", "set", "unset"),
        metavar="VERB",
        help="show (the default), set or unset",
    )
    s.add_argument("items", nargs="*", metavar="KEY[=VALUE]")


def register_config(cli):
    command, group = cli.command, cli.group
    g = group("library", "this library's settings")
    s = command(
        g,
        "config",
        "the library's settings (config.toml): show them, set KEY=VALUE "
        f"({', '.join(vmsettings.CONFIG_KEYS)}) or unset lan_parent",
        cmd_library_config,
        usage="beanjar library config [show | set KEY=VALUE... | unset KEY...] [--json]",
    )
    s.add_argument(
        "verb",
        nargs="?",
        choices=("show", "set", "unset"),
        metavar="VERB",
        help="show (the default), set or unset",
    )
    s.add_argument("items", nargs="*", metavar="KEY[=VALUE]")

    g = group("host", "this machine's settings, every library's")
    s = command(
        g,
        "config",
        "this machine's settings ($XDG_CONFIG_HOME/beanjar/config.toml): max_running=N, how "
        "many VMs may run at once on this host (every library's, the development VMs' and the "
        "regress harness's count); unset max_running for no limit (the default). "
        "memory_headroom_mib=N, positive MiB reserved beyond each VM's RAM for admission and "
        "the KVM soft memory limit (default 1024). "
        "firmware_dirs=DIR[:DIR...], the host's firmware collections, where create --firmware NAME, "
        "the launcher and regress look a firmware up (default /srv/beanjar/firmware)",
        cmd_host_config,
        usage="beanjar host config [show | set KEY=VALUE... | unset KEY...] [--json]",
    )
    s.add_argument(
        "verb",
        nargs="?",
        choices=("show", "set", "unset"),
        metavar="VERB",
        help="show (the default), set or unset",
    )
    s.add_argument("items", nargs="*", metavar="KEY[=VALUE]")
    s.set_defaults(library_needed=False)  # needs no library, and makes none
    s = command(
        g,
        "prune-layer-images",
        "remove the cached layer images (~/.cache/beanjar/layer-images, a checkout's "
        "build/layer-images) that no VM of this library uses, versions pinned in vm.toml "
        "included; refused while any VM runs or starts; the package's shipped images stay",
        cmd_host_prune_layer_images,
        usage="beanjar host prune-layer-images [--dry-run] [--json]",
    )
    s.add_argument("--dry-run", action="store_true", help="list what would be removed")
    s = command(
        g,
        "add",
        "add a remote host: beanjar runs its own beanjar there over ssh (key-based login). URI is "
        "ssh://[USER@]HOST[:PORT]; HOST may be an alias of ~/.ssh/config",
        cmd_host_add,
        usage="beanjar host add NAME URI [--json]",
    )
    s.add_argument(
        "host_name", metavar="NAME", help=f"the host's name ({remote.LOCAL!r} is this machine)"
    )
    s.add_argument("uri", metavar="URI", help="ssh://[USER@]HOST[:PORT]")
    s.set_defaults(library_needed=False)
    s = command(
        g,
        "remove",
        "forget a remote host",
        cmd_host_remove,
        usage="beanjar host remove NAME [--json]",
    )
    s.add_argument("host_name", metavar="NAME")
    s.set_defaults(library_needed=False)
    s = command(g, "list", "the remote hosts: name and URI", cmd_host_list)
    s.set_defaults(library_needed=False)
