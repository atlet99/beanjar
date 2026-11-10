# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""A VM's settings (vm.toml): their names, values, help and defaults, the checks of
a whole configuration (validate), and the launcher environment they make (launcher_env)."""

import re

from . import nvm, targets, vmconfig
from .base import (
    Error,
)
from . import vmidentity


DISPLAYS = ("dbus", "egl-headless", "none")


AUDIOS = ("dbus", "none", "off")


# gpu (qemu.py GPU_DEVICE): vhost-user-gpu runs the GPU in its own process; auto is vhost-user-gpu on
# ARM hosts with a render node, else virtio-gpu-gl; docs/dev/architecture/gpu-process.md
GPUS = ("auto", "vhost-user-gpu", "virtio-gpu-gl")


# free_page_reporting (qemu.py FREE_PAGE_REPORTING): off by default
ONOFF = ("on", "off")


# mitm_flows (qemu.py MITM_FLOWS): how many flows the mode=mitm inspector keeps, 0 for all of them;
# the default is not written out
MITM_FLOWS = 1000
# mitm_flow_bytes (qemu.py MITM_FLOW_BYTES): the bytes of bodies those flows may hold, as "200M";
# unset (the default) is no limit


def byte_size(text):
    """A size as vm.toml's network.mitm_flow_bytes writes it: bytes, or a number with K, M or G
    (KiB, MiB, GiB), e.g. 200M; 0 or empty is none. ValueError if it is not one."""
    m = re.fullmatch(r"\s*(\d+)\s*([KMG]?)\s*", str(text).upper())
    if not str(text).strip():
        return 0
    if not m:
        raise ValueError(f"{text!r}: a size such as 200M (bytes, or K, M, G)")
    return int(m.group(1)) << {"": 0, "K": 10, "M": 20, "G": 30}[m.group(2)]


def flow_bytes_label(value):
    """mitm_flow_bytes as the summary shows it: "200 MiB", or "no limit"."""
    n = byte_size(value or "")
    if not n:
        return "no limit"
    for shift, unit in ((30, "GiB"), (20, "MiB"), (10, "KiB")):
        if n >= 1 << shift:
            return f"{n / (1 << shift):g} {unit}"
    return f"{n} bytes"


# The network (docs/dev/architecture/network.md): link, where the guest's NIC is attached (nat: behind
# this host; lan: on lan_parent's wired LAN, a TV of its own there; off: nothing), and mode, whether
# its traffic passes through the proxy (mitm) or not (direct). mode means nothing with link off.
LINKS = ("nat", "lan", "off")


MODES = ("mitm", "direct")


LAN_NOT_IMPLEMENTED = "link=lan with mode=direct is not implemented yet"


def guest_net(link, mode):
    """What the guest's network is, link and mode together: off, mitm or direct."""
    return "off" if (link or "nat") == "off" else (mode or "mitm")


def network_label(link, mode):
    """link and mode as `beanjar list` and the GUI's summary show them: nat+mitm, lan+direct, off."""
    link = link or "nat"
    return "off" if link == "off" else f"{link}+{mode or 'mitm'}"


def dns_label(name):
    """A VM's name as a DNS label, its DHCP hostname with link lan: lower-case, anything outside
    a-z, 0-9 and "-" ("." and "_" in a name) made "-", none at either end, 63 characters at most."""
    return re.sub(r"[^a-z0-9-]", "-", name.lower())[:63].strip("-")


# The layers. guest/initramfs/rootfs/init stacks the ones LAYERS names, in
# that order, highest priority first (the first overlay lowerdir: its files hide those of the
# layers after it and of the stock rootfs), then targets.ALWAYS_LAYERS, as the launcher's boot.conf
# lists them; a layer named twice is taken at its first place. Above them all is the per-VM layer.
DEFAULT_LAYERS = ("normal", "gl")


# Each layer's description, requires, conflicts and forwards are its own, in guest/layers/<name>/layer.toml
# (layermeta.py). The optional layers' order in the dialogs, after the VM's own; others follow by name.
LAYER_ORDER = ("normal", "gl", "headless", "devmode", "rootssh", "hbchannel")


# debug_mode (qemu.py DEBUG_MODE): the TV's debug status as the board's debugMode=N word has it
DEBUG_MODES = nvm.DEBUG_MODES


DEFAULT_DEBUG_MODE = "debug"


# vm.toml: the VM's settings (these keys, in this order, then the manager's own, e.g. created) and
# the launcher's environment variable each one is (launch.py --dir sets them with vm_launcher_env).
# layers is an array, LAYERS its names joined with commas. Any upper-case key is a launcher
# variable passed through as it is (e.g. PMLOG_DEBUG = "1", TCG_CPU = "max").
SETTINGS = {
    "fw": "FW",
    "broadcast": "BROADCAST",
    "mem": "MEM",
    "smp": "SMP",
    "free_page_reporting": "FREE_PAGE_REPORTING",
    "debug_mode": "DEBUG_MODE",
    "layers": "LAYERS",
    "link": "NET_LINK",
    "mode": "NET_MODE",
    "display": "DISPLAY_BACKEND",
    "gpu": "GPU_DEVICE",
    "audio": "AUDIO",
    "mac": "MAC",
    "serial": "SERIAL",
    "listen": "LISTEN_ADDRESS",
    "forward": "FORWARD",
    "address": "NET_ADDRESS",
    "mitm_flows": "MITM_FLOWS",
    "mitm_flow_bytes": "MITM_FLOW_BYTES",
}


# Each setting as `beanjar settings --help` describes it: (its values, its default, what it does,
# what `settings NAME unset KEY` brings back, or None where it has no unset). Configure's empty value
# for these keys is that unset; the others refuse an empty value or (layers) take it as no layers.
SETTING_HELP = {
    "fw": (
        "DIR",
        "the VM's own firmware/, which a VM created in place (create --firmware) does not have",
        "an external directory of extracted paks, booted in place and never written",
        "the VM's own firmware/ (none on a VM created in place, which then has no firmware)",
    ),
    "broadcast": (
        "|".join(targets.BROADCASTS),
        "the firmware's own, else the chip's default (US where it ships on US models)",
        "boot a global firmware as that broadcast standard's model",
        "the firmware's own, else the chip's default",
    ),
    "mem": ("MIB", "the firmware's model's own", "guest RAM in MiB", None),
    "smp": ("N", str(vmconfig.DEFAULTS["smp"]), "CPUs", None),
    "free_page_reporting": (
        "|".join(ONOFF),
        "off",
        "hand the guest's free memory back to the host",
        None,
    ),
    "debug_mode": (
        "|".join(DEBUG_MODES),
        "debug",
        "the TV's debug status, the kernel's debugMode= and the EEPROM's (webOS 1 to 3.9 and 5+; webOS 4.x: the kernel's only): debug is a development "
        "set, with faultmanager's crash popup; event a retail set that keeps event logs, still "
        "with the popup; release a retail set, no popup, and it reboots when tvservice or lginit_app "
        "exits",
        "debug",
    ),
    "layers": (
        "NAME[@VERSION],...",
        ",".join(vmconfig.DEFAULTS["layers"]),
        (
            "guest layers, highest priority first; empty for none. NAME@VERSION picks a versioned "
            "layer's version (hbchannel@0.7.3), plain NAME its default; a newer Homebrew Channel "
            "in the VM's state (one it updated itself to) stays when the layer's is older"
        ),
        None,
    ),
    "link": ("|".join(LINKS), vmconfig.DEFAULTS["link"], "where the NIC is attached", None),
    "mode": (
        "|".join(MODES),
        vmconfig.DEFAULTS["mode"],
        "the traffic inspector or none; ignored with link=off",
        None,
    ),
    "display": ("|".join(DISPLAYS), vmconfig.DEFAULTS["display"], "the display backend", None),
    "gpu": (
        "|".join(GPUS),
        "auto",
        "where the GPU renders: vhost-user-gpu in its own process (ARM hosts with a render node; "
        "a VM run without its unit loses the display at a guest restart), virtio-gpu-gl on QEMU's "
        "main loop; auto is vhost-user-gpu on ARM hosts, else virtio-gpu-gl",
        "auto",
    ),
    "audio": (
        "|".join(AUDIOS),
        "dbus with display=dbus, else none",
        "the audio backend; dbus needs display=dbus",
        None,
    ),
    "mac": ("AA:BB:CC:DD:EE:FF", "made at create", "the board MAC (unicast)", None),
    "serial": ("SERIAL", "made at create", "the serial number, 1 to 16 letters and digits", None),
    "listen": (
        "ADDRESS",
        "the VM's own address",
        "link=nat: where its TV-service ports (SSAP 3000/3001, SSH 6622, developer mode "
        "9922/9991) listen, e.g. 0.0.0.0 for the LAN",
        "the VM's own address",
    ),
    "forward": (
        "PORT[:GUEST],...",
        "none",
        "guest TCP ports relayed on the VM's address as its TV-service ports are (on listen with "
        "link=nat, also on the LAN address with link=lan), e.g. 8080,3000 or 8081:80 for guest 80 "
        "on host 8081; a host port a layer has (SSAP 3000/3001, SSH 6622, 9922, 9991), 5900 (VNC), "
        "18083 (the inspector) or one below 1024 is refused",
        "none",
    ),
    "address": (
        "dhcp|A.B.C.D/NN,GATEWAY",
        "dhcp",
        "link=lan: the VM's LAN address",
        "dhcp",
    ),
    "mitm_flows": (
        "N",
        str(MITM_FLOWS),
        "how many flows the mode=mitm inspector keeps, 0 for all",
        str(MITM_FLOWS),
    ),
    "mitm_flow_bytes": (
        "SIZE",
        "no limit",
        "the bytes of bodies those flows may hold, e.g. 200M, 0 for no limit",
        "no limit",
    ),
}


assert set(SETTING_HELP) == set(SETTINGS), "every setting has its help"


# The launcher's own variables, which launch.py sets from the VM directory.
LAUNCHER_OWN = {
    "STATE",
    "RUN_DIR",
    "LOG_DIR",
    "SAVED_LOG_DIR",
    "QEMU_PIDFILE",
    "CONSOLE_SOCK",
    "CONSOLE_LOG",
    "WEBOS_LOG",
    "QMP_SOCK",
    "VIEWER_QMP_SOCK",
    "CURSOR_SOCK",
    "VIDEO_SOCK",
    "IR_SOCK",
    "MEDIA_SOCK",
    "CAPTURE_SOCK",
    "DBUS_ADDR",
    "NETSTUB_LOG",
    "NETNS_LOG_DIR",
    "FW_CACHE",
    "BUILD",
    "RUN",
    "IDENTITY_DIR",
    "NETSTUB_CA",
    "SLOT",
    "ADDRESS_PREFIX",
    "VM_ADDRESS",
    "NET_LAN_PARENT",
    "NET_LAN_MAC",
    "NET_LAN_HOSTNAME",
}


# The launcher's variables that are the checkout VM's experiments (host/scripts/vm-launcher), not a VM's: the
# uncompressed rootfs (fwcache.py), whose images a VM does not keep.
CHECKOUT_ONLY = {"ROOTFS", "UNCOMPRESSED_PAKS"}


CONFIG_KEYS = ("repo", "auto_reconnect", "epk2extract", "mac_prefix", "lan_parent", "addresses")


MANIFEST_KEYS = (
    "name",
    "chip",
    "release",
    "epk",
    "epk_size",
    "sha256",
    "source",
    "paks",
    "imported",
)


# A VM's tags (vm.toml's metadata.tags; create --tag, beanjar tag): short labels for finding and
# sorting VMs (list --tag), kept with its settings and copied by clone. Lower-case letters, digits
# and dashes; conventions only (host/core/README.md): scratch, keep.
TAG_RE = re.compile(r"[a-z0-9][a-z0-9-]{0,31}")


def parse_tag(text):
    """A tag as it is kept: lower-cased; Error unless letters, digits and dashes, at most 32, not
    starting with a dash."""
    tag = str(text).strip().lower()
    if not TAG_RE.fullmatch(tag):
        raise Error(
            f"tag {text!r}: letters, digits and dashes, at most 32, starting with a letter or digit",
            "tags",
        )
    return tag


def as_tags(v):
    """tags as a list without repeats, in the order given: a list, or a string split at commas."""
    if isinstance(v, str):
        v = v.split(",")
    out = []
    for t in v or ():
        t = parse_tag(t)
        if t not in out:
            out.append(t)
    return out


def as_layers(v):
    """layers as a list: a list as it is, a string split at commas (the CLI's --layers)."""
    if isinstance(v, str):
        return [n for n in v.split(",") if n]
    return list(v)


def launcher_env(cfg):
    """vm.toml's settings as the launcher's environment: {variable: value}."""
    env = {}
    for k, v in cfg.items():
        if k in SETTINGS:
            var = SETTINGS[k]
        elif k == "slot":
            var = "SLOT"  # the VM's host address (qemu.py)
        elif re.fullmatch(r"[A-Z][A-Z0-9_]*", k):
            var = k
            if k in LAUNCHER_OWN or k in SETTINGS.values():
                raise Error(f"{k} is set by the VM manager, not in vm.toml", k)
            if k in CHECKOUT_ONLY:
                raise Error(
                    f"{k} is an experiment of the checkout's VM (host/scripts/vm-launcher), not in vm.toml",
                    k,
                )
        else:
            continue  # the manager's own (created)
        if isinstance(v, list) and all(isinstance(i, str) for i in v):
            v = ",".join(v)
        if isinstance(v, bool) or not isinstance(v, (str, int, float)):
            raise Error(
                f"{k} must be a string or a number"
                + (" (or, layers, an array of names)" if k == "layers" else ""),
                k,
            )
        env[var] = str(v)
    return env


def validate(cfg, saved=False):
    """Error (with the setting's key) if cfg is not a VM's settings. The layers are checked against
    their layer.toml (layermeta.check); saved: cfg is read back from a vm.toml, whose layers may name
    one this tree lacks (the dialogs show it and keep it until it is switched off)."""
    checks = (
        ("platform", targets.PLATFORMS),
        ("link", LINKS),
        ("mode", MODES),
        ("display", DISPLAYS),
        ("gpu", GPUS),
        ("audio", AUDIOS),
        ("free_page_reporting", ONOFF),
        ("debug_mode", tuple(DEBUG_MODES)),
    )
    for key, allowed in checks:
        if key in cfg and cfg[key] not in allowed:
            raise Error(f"{key}={cfg[key]}: one of {', '.join(allowed)}", key)
    for key in ("mem", "smp"):
        if key in cfg and (
            isinstance(cfg[key], bool) or not isinstance(cfg[key], int) or cfg[key] <= 0
        ):
            raise Error(f"{key} must be a positive number", key)
    if "mitm_flows" in cfg and (
        isinstance(cfg["mitm_flows"], bool)
        or not isinstance(cfg["mitm_flows"], int)
        or cfg["mitm_flows"] < 0
    ):
        raise Error("mitm_flows must be a number, 0 for no limit", "mitm_flows")
    if "mitm_flow_bytes" in cfg:
        try:
            byte_size(cfg["mitm_flow_bytes"])
        except ValueError as e:
            raise Error(f"mitm_flow_bytes={e}", "mitm_flow_bytes") from None
    if cfg.get("audio") == "dbus" and cfg.get("display") != "dbus":
        raise Error("audio=dbus needs display=dbus", "audio")
    if "slot" in cfg:
        from . import keys

        if isinstance(cfg["slot"], bool) or cfg["slot"] not in keys.SLOTS:
            raise Error(
                f"slot {cfg['slot']!r}: a number from {keys.SLOTS[0]} to {keys.SLOTS[-1]}", "slot"
            )
    if "mac" in cfg:
        vmidentity.parse_mac(cfg["mac"])
    if "serial" in cfg:
        vmidentity.parse_serial(cfg["serial"])
    if "tags" in cfg:
        tags = cfg["tags"]
        if not isinstance(tags, list) or not all(isinstance(t, str) for t in tags):
            raise Error("tags must be an array of strings", "tags")
        if as_tags(tags) != tags:
            raise Error(f"tags {tags}: lower-case, each once", "tags")
    link = cfg.get("link", "nat")
    if "listen" in cfg:
        from . import keys

        if link == "lan":
            raise Error(
                "listen is for link=nat: a link=lan VM's ports are on its own LAN address", "listen"
            )
        try:
            keys.listen_address(cfg["listen"])
        except keys.Error as e:
            raise Error(str(e), "listen") from None
    if "forward" in cfg:
        from . import layermeta

        if not isinstance(cfg["forward"], str):
            raise Error("forward must be a string of ports, e.g. 8080,8081:80", "forward")
        try:
            layermeta.parse_forward(cfg["forward"])
        except layermeta.Error as e:
            raise Error(str(e), "forward") from None
    if "address" in cfg:
        from . import keys

        if link != "lan":
            raise Error(f"address is for link=lan only (this VM is link={link})", "address")
        try:
            keys.lan_address(cfg["address"])
        except keys.Error as e:
            raise Error(str(e), "address") from None
    layers = cfg.get("layers", [])
    if not isinstance(layers, list) or not all(
        isinstance(n, str) and re.fullmatch(r"[a-z0-9_-]+(@[0-9A-Za-z][0-9A-Za-z._+-]*)?", n)
        for n in layers
    ):
        raise Error(
            "layers is a list of layer names (a-z, 0-9, '_', '-'), each NAME or NAME@VERSION",
            "layers",
        )
    from . import layermeta

    try:
        layermeta.check(layers, unknown_ok=saved)
    except layermeta.Error as e:
        raise Error(str(e), "layers") from None


def layer_list(layers, available):
    """The rows of the dialogs' layer list: [(name, on)], the names layers has first, in its order
    (on), then the other optional layers of available (Library.layers()), off, in LAYER_ORDER and
    then by name. A layer that guest/initramfs/rootfs/init adds by itself is listed only where layers has it."""
    rows = []
    for n in layers or ():
        if n and n not in (r[0] for r in rows):
            rows.append((n, True))
    auto = set(targets.ALWAYS_LAYERS)
    rank = {n: i for i, n in enumerate(LAYER_ORDER)}
    named = {r[0].partition("@")[0] for r in rows}  # NAME@VERSION is NAME's row
    rest = sorted(
        (n for n in available if n not in auto and n not in named),
        key=lambda n: (rank.get(n, len(rank)), n),
    )
    return rows + [(n, False) for n in rest]
