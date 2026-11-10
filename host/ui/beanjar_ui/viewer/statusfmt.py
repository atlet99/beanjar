# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""What the status bar's icons say, without GTK:
an icon's state and its tooltip from a stats.Source."""

from . import stats as statslib

OFF, IDLE, ACTIVE = "off", "idle", "active"
DIRECT = "direct"
TITLES = ("CPU", "GPU", "Memory", "Disk", "Network")
METERS = (statslib.CPU, statslib.GPU, statslib.MEM)  # drawn as meters, not icons
# The icon of each indicator (the system theme's, except cpu-symbolic, ui/cpu-symbolic.svg), by state.
ICONS = (
    {OFF: "cpu-symbolic", IDLE: "cpu-symbolic", ACTIVE: "cpu-symbolic"},
    {OFF: "cpu-symbolic", IDLE: "cpu-symbolic", ACTIVE: "cpu-symbolic"},  # a meter as CPU's
    {OFF: "cpu-symbolic", IDLE: "cpu-symbolic", ACTIVE: "cpu-symbolic"},  # a meter
    {
        OFF: "drive-harddisk-symbolic",
        IDLE: "drive-harddisk-symbolic",
        ACTIVE: "drive-harddisk-symbolic",
    },
    {
        OFF: "network-offline-symbolic",
        IDLE: "network-idle-symbolic",
        ACTIVE: "network-transmit-receive-symbolic",
        DIRECT: "network-wired-symbolic",
    },
)


def rate(bps):
    for unit, size in (("G", 1e9), ("M", 1e6), ("K", 1e3)):
        if bps >= size:
            return f"{bps / size:.1f}{unit}/s"
    return f"{bps:.0f}B/s" if bps else "0"


def state(src):
    """OFF: no source; ACTIVE: the counter moved in the last refresh."""
    if src.network_mode == "direct":
        return DIRECT
    if not src.ok:
        return OFF
    return ACTIVE if src.moved else IDLE


def tooltip(kind, src):
    """The title and numbers, or why the source is unavailable."""
    title = TITLES[kind]
    if kind == statslib.GPU:
        value = src.gpu
        if not value["available"]:
            return "GPU: on, usage unavailable" if value["enabled"] else "GPU: off"
        driver = f" ({value['driver']})" if value["driver"] else ""
        if value["headline"] is None:
            return f"GPU{driver}: measuring"
        engines = statslib.gpu.engines(value)  # in a stable order while idle
        memory = statslib.gpu.memory(value)
        return "\n".join(
            [f"GPU{driver}: {value['headline']:.0f}%"]
            + [f"{name}: {percent:.0f}%" for name, percent in engines]
            + ([f"Memory: {memory}"] if memory else [])
        )
    if kind == statslib.MEM:
        return memory_tooltip(src)
    if kind == statslib.NET and src.network_mode == "direct":
        return "Network: Direct networking\nTraffic rates unavailable"
    if not src.ok:
        return f"{title}: {src.why}"
    if kind == statslib.CPU:
        cores = "".join(
            f"\nvCPU {index}: " + ("measuring" if value is None else f"{value:.0f}%")
            for index, value in src.cores
        )
        return f"{title}\n{src.a:.0f}% of one host CPU" + cores
    if kind == statslib.DISK:
        return f"{title}\nRead {rate(src.a)}\nWrite {rate(src.b)}"
    return f"{title} (guest)\nDownload {rate(src.a)}\nUpload {rate(src.b)}"


def mib(kib):
    return f"{kib / 1024:.0f}"


def used(value, total=None):
    """ "812 of 1990 MiB (41%)" of a dict with "used" and "total" KiB."""
    total = value["total"] if total is None else total
    return f"{mib(value['used'])} of {mib(total)} MiB ({value['used'] / total * 100:.0f}%)"


def memory_tooltip(src):
    """The guest's RAM and swap (the meter's bars), then what the VM holds on the host."""
    value = src.memory or {}
    ram, swap, host = value.get("guest"), value.get("swap"), value.get("host")
    if ram and ram["total"]:
        lines = ["Memory (guest)", f"RAM: {used(ram)} in use"]
    else:
        lines = [f"Memory: {src.why}"]
    if not value:
        return lines[0]
    if swap is None:
        lines.append(f"Swap: unknown ({value.get('swap_why') or 'no report'})")
    elif not swap["total"]:
        lines.append("Swap: none")
    else:
        lines.append(f"Swap: {used(swap)} in use")
    if host:
        lines += host_lines(host["ram"], host["swap"])
    return "\n".join(lines)


def host_lines(ram, swap):
    """The VM's host footprint: QEMU's resident memory (its unit's, with the page cache charged
    to it) and swap, and what each is measured against."""
    if ram["why"] or ram["used"] is None or not ram["total"]:
        lines = [f"Host: {ram['why'] or 'unavailable'}"]
    else:
        where = "the VM's unit" if ram["source"] == "unit" else "QEMU's resident memory"
        lines = [f"Host: {used(ram)}, {where}, against {ram['of']}"]
        if ram.get("parts"):
            lines.append(f"({ram['parts']})")
        if ram.get("cache") is not None:
            lines.append(f"Page cache: {mib(ram['cache'])} MiB of it")
    if swap["why"] or swap["used"] is None or not swap["total"]:
        lines.append(f"Host swap: {swap['why'] or 'unavailable'}")
    else:
        where = "the VM's unit" if swap["source"] == "unit" else "QEMU"
        lines.append(f"Host swap: {used(swap)}, {where}, against {swap['of']}")
    return lines
