# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The VMs' life: list, create, start, reason, stop, info, tag, delete, rename, prepare, wipe."""

import argparse
import os
import shlex

from .. import core, hostprocs, keys, poweroff, remote, vmsettings, vmunits
from ..core import Error, fmt_duration
from .common import Usage, emit, kv_lines, layer_versions, progress, table, warn
from .forward import bare_new_name
from .settings import settings_of, warn_mitm


def vm_row(vm):
    """A library VM as `list` shows it."""
    state, detail = vm.state(qmp=True)
    c = vm.config
    unit = vm.unit_name() if state != core.OFF else vmunits.vm_unit_name(vm.dir)
    return {
        "name": vm.name,
        "library": vm.library.root,
        "tag": vmunits.library_tag(vm.library.root),
        "unit": unit,
        "state": state,
        "detail": detail,
        "platform": vm.platform_title(),
        "touch": vm.touch(),  # a touch screen: the web viewer sends its touches to it
        "portrait": vm.portrait(),  # turns to portrait: the viewers turn the picture
        "fw": vm.firmware_dir(),
        "firmware": vm.firmware_name(),
        "fw_external": vm.firmware_external(),
        "mem": vm.mem(),
        "smp": c.get("smp"),
        "display": c.get("display", ""),
        "network": vmsettings.network_label(c.get("link"), c.get("mode")),
        "address": vm.address(),
        "lan_address": vm.lan_address(),
        "pid": vm.pid(),
        "uptime": vm.uptime(),
        "viewer": vm.viewer_pid(),
        "vnc": vm.vnc_pid(),
        "layers": c.get("layers"),  # None: the checkout's for its firmware
        # {layer: version} of its versioned layers (hbchannel@VERSION, plain: the default)
        "layer_versions": layer_versions(c.get("layers")),
        "reason": vm.reason(),  # why it runs (vmunits.unit_reason), None
        "tags": c.get("tags", []),
    }


def unit_row(name, props):
    """A VM's unit in beanjar.slice that is no library's VM (a regress target's, one of another
    RUN_DIR): what its unit and its main process's environment (the launcher's) tell."""
    from .. import keys, netns, nvm, targets, paths

    tag, vm_name = vmunits.unit_tag(name)
    env = hostprocs.proc_env(int(props.get("MainPID") or 0) or None)
    run_dir = vmunits.unit_run_dir(props) or ""
    pid = core.read_pid(env.get("QEMU_PIDFILE") or os.path.join(run_dir, "qemu.pid"))
    if not (core.proc_alive(pid, "qemu-system") and core.in_unit(pid, name)):
        pid = None
    try:
        with open(
            env.get("LOG") or os.path.join(env.get("LOG_DIR") or run_dir, "console.log"), "rb"
        ) as f:
            booted = b"debug shell" in f.read()
    except OSError:
        booted = False
    active = props.get("ActiveState")
    state = (
        core.STOPPING
        if active == "deactivating"
        else core.RUNNING
        if pid and booted
        else core.STARTING
    )
    # FW as the launcher resolves it (a bare name, a path): in the unit's checkout, its working directory
    try:
        tree = paths.Tree(os.readlink(f"/proc/{int(props.get('MainPID') or 0)}/cwd"))
    except (OSError, ValueError):
        tree = paths.TREE
    fw, platform, mem = env.get("FW") or "", "", env.get("MEM")
    try:
        fw = os.path.join(tree.top, tree.firmware(fw)[0])
        target = paths.firmware_target(fw)
        platform = targets.platform_title(target)
        mem = mem or nvm.board_ram(
            target, tree, targets.board_broadcast(target, fw, env.get("BROADCAST"))
        )
    except Exception:  # a firmware this tree does not boot: shown as far as it is known
        pass
    m = core.read_toml(os.path.join(fw, "manifest.toml"), None) if fw else None
    slot = env.get("SLOT")
    return {
        "name": vm_name,
        "library": None,
        "tag": tag or "",
        "unit": name,
        "run_dir": run_dir,
        "instance": env.get("INSTANCE"),
        "state": state,
        "detail": "",
        "platform": platform,
        "fw": fw,
        "firmware": str((m or {}).get("name") or os.path.basename(fw.rstrip("/"))),
        "fw_external": False,
        "mem": int(mem) if str(mem or "").isdigit() else None,
        "smp": env.get("SMP"),
        "display": env.get("DISPLAY_BACKEND", ""),
        "network": vmsettings.network_label(env.get("NET_LINK"), env.get("NET_MODE")),
        "address": keys.address(int(slot), env.get("ADDRESS_PREFIX"))
        if str(slot or "").isdigit()
        else None,
        "lan_address": (netns.lan_info(run_dir) or {}).get("address") if run_dir else None,
        "pid": pid,
        "uptime": hostprocs.uptime_of(pid) if pid else None,
        "viewer": None,
        "vnc": None,
        "layers": env["LAYERS"].split(",") if env.get("LAYERS") else None,
        "layer_versions": layer_versions(env["LAYERS"].split(",") if env.get("LAYERS") else None),
        "reason": vmunits.unit_reason(name),
        "tags": [],
    }


def local_rows(lib, a):
    from .. import gpu

    rows = [vm_row(vm) for vm in lib.vms()]
    if a.all:
        # every other VM up in beanjar.slice: other libraries' (the development VMs'), the regress
        # harness's, the tests'
        shown = {r["unit"] for r in rows} | {f"webOS VM {vm.run_dir}" for vm in lib.vms()}
        for name, props in sorted(vmunits.vm_units().items()):
            if name in shown or props.get("Description") in shown:
                continue
            vm = core.unit_vm(props)
            rows.append(vm_row(vm) if vm else unit_row(name, props))
    for r, status in zip(rows, gpu.measure([(r["pid"], r["layers"]) for r in rows])):
        r["gpu"] = status
        r["host"] = None
    return rows


def list_sources(a):
    """The hosts `list` asks besides this machine: {name: uri}; none with --local or --host local."""
    if a.local or a.host == remote.LOCAL:
        return {}
    configured = remote.hosts()
    if a.host:
        if a.host not in configured:
            raise Error(f"no host named {a.host} (beanjar host list)")
        return {a.host: configured[a.host]}
    return configured


def cmd_list(lib, a):
    from concurrent.futures import ThreadPoolExecutor

    from .. import gpu

    targets = list_sources(a)
    with_local = not a.host or a.host == remote.LOCAL
    failed = 0
    if not targets:
        rows = local_rows(lib, a)
    else:
        rows = []
        with ThreadPoolExecutor(len(targets)) as pool:
            futures = {h: pool.submit(remote.list_rows, h, u, a.all) for h, u in targets.items()}
            if with_local:
                try:
                    rows = local_rows(lib, a)
                except Error as e:
                    warn(f"{remote.LOCAL}: {e}")
                    failed += 1
            for h, future in futures.items():
                try:
                    rows += future.result()
                except Error as e:
                    warn(str(e) if isinstance(e, remote.HostError) else f"host {h}: {e}")
                    failed += 1
        if failed == len(targets) + with_local:
            return 1  # nothing could be listed: no result at all
    none = f"no VMs in {lib.root}" + (" and none running elsewhere" if a.all else "")
    if targets:
        none = "no VMs on this machine or the hosts" if with_local else f"no VMs on {a.host}"
    if a.tag:
        want = vmsettings.as_tags(a.tag)
        rows = [r for r in rows if set(want) <= set(r.get("tags") or ())]
        none = f"{none} tagged {', '.join(want)}" if none.startswith("no VMs") else none
    if a.json or not rows:
        return emit(a, rows, none)
    columns = [
        ("NAME", lambda r: r["name"], False),
        (
            "STATE",
            lambda r: (
                r["state"]
                + (f" ({r['detail']})" if r["detail"] else "")
                + (" +viewer" if r["viewer"] else "")
                + (" +vnc" if r["vnc"] else "")
            ),
            False,
        ),
        ("PLATFORM", lambda r: r["platform"] or "", False),
        ("MEM", lambda r: str(r["mem"] or ""), True),
        ("SMP", lambda r: str(r["smp"] or ""), True),
        ("GPU", lambda r: gpu.short(r["gpu"]) if r["pid"] else "", True),
        ("DISPLAY", lambda r: r["display"] or "", False),
        ("NETWORK", lambda r: r["network"] or "", False),
        ("ADDRESS", lambda r: r["lan_address"] or r["address"] or "", False),
        ("UPTIME", lambda r: str(fmt_duration(r["uptime"])), True),
        (
            "FIRMWARE",
            lambda r: r["firmware"] + (f" (in place: {r['fw']})" if r["fw_external"] else ""),
            False,
        ),
        ("TAGS", lambda r: ",".join(r.get("tags") or ()), False),
        ("REASON", lambda r: short_reason(r.get("reason")), False),
    ]
    if any(r.get("layer_versions") for r in rows):
        # which version of a versioned layer each VM runs, after FIRMWARE
        columns.insert(
            [h for h, _f, _r in columns].index("FIRMWARE") + 1,
            (
                "VERSIONS",
                lambda r: ",".join(f"{n}@{v}" for n, v in (r.get("layer_versions") or {}).items()),
                False,
            ),
        )
    if a.all:
        columns.insert(0, ("LIBRARY", lambda r: r["tag"] or "", False))
    if targets:
        columns.insert(0, ("HOST", lambda r: r["host"] or remote.LOCAL, False))
    return emit(a, rows, table(rows, columns))


REASON_COLUMN = 32  # list's REASON: the text cut to this; --json and info carry it whole


def short_reason(reason):
    text = (reason or {}).get("text") or ""
    return text if len(text) <= REASON_COLUMN else text[: REASON_COLUMN - 1] + "\u2026"


def cmd_create(lib, a):
    host, name = remote.split_spec(a.name)
    if host == remote.LOCAL:
        a.name = name
    elif host is not None and host in remote.hosts():
        raise Usage(
            "create makes a VM on this machine only (its firmware is a directory here): "
            f"run beanjar create {name} on the host"
        )
    vm = lib.create(a.name, fw=a.firmware, place=not a.import_, out=progress, **settings_of(a))
    emit(a, {"name": vm.name, **vm.summary()})
    warn_mitm(vm)
    return 0


def cmd_start(lib, a):
    vm = lib.start(a.name, out=progress, args=a.args, reason=a.reason, by=a.by)
    warn_mitm(vm)
    state, pid = vm.state()[0].lower(), vm.pid()
    return emit(
        a,
        {"name": vm.name, "state": state, "pid": pid, "reason": vm.reason()},
        f"{vm.name}: {state} (pid {pid})",
    )


def cmd_stop(lib, a):
    lib.stop(
        a.name,
        core.UNPLUG if a.unplug else core.POWER_OFF,
        fallback=not a.no_fallback,
        out=progress,
        force=a.force,
    )
    return emit(a, {"name": a.name, "state": core.OFF.lower()}, f"{a.name}: off")


def cmd_tag(lib, a):
    """A VM's tags: shown, or changed by +TAG (or TAG) and -TAG words."""
    add, remove = [], []
    for w in a.changes:
        if w == "--json":
            a.json = True
        elif w.startswith("--"):
            raise Usage(f"tag NAME [+TAG | -TAG ...]: unknown option {w}")
        elif w.startswith("-"):
            remove.append(w[1:])
        else:
            add.append(w.removeprefix("+"))
    tags = lib.tag(a.name, add, remove) if add or remove else lib.vm(a.name).config.get("tags", [])
    return emit(a, {"name": a.name, "tags": tags}, f"{a.name}: " + (", ".join(tags) or "no tags"))


def cmd_reason(lib, a):
    """Why a running VM runs: show it, set it (TEXT) or clear it ("")."""
    vm = lib.vm(a.name)
    if a.text is not None:
        vm.set_reason(a.text, a.by)
    reason = vm.reason()
    return emit(
        a,
        {"name": vm.name, "reason": reason},
        f"{vm.name}: " + (vmunits.reason_line(reason) if reason else "no reason"),
    )


def cmd_prepare(lib, a):
    lib.prepare(a.name, out=progress, force=not a.if_stale)
    return emit(a, {"name": a.name, "prepared": True}, f"{a.name}: board data prepared")


def cmd_wipe(lib, a):
    lib.wipe(a.name, a.part, out=progress)
    return emit(
        a,
        {"name": a.name, "parts": a.part or None},
        f"{a.name}: wiped " + (", ".join(a.part) if a.part else "every state disk"),
    )


def cmd_delete(lib, a):
    lib.delete(a.name)
    return emit(a, {"name": a.name, "deleted": True}, f"deleted {a.name}")


def cmd_rename(lib, a):
    bare_new_name(a)
    old = a.name
    vm = lib.rename(old, a.new)
    lan = vm.lan_hostname()
    text = f"{old} renamed to {vm.name}"
    if lan:
        text += f"; its LAN hostname is {lan} from its next start"
    return emit(a, {"name": vm.name, "renamed_from": old, "dir": vm.dir, "lan_hostname": lan}, text)


def info_data(vm):
    """One VM as `info` shows it."""
    from .. import gpu

    state, detail = vm.state(qmp=True)
    pid = vm.pid()
    return {
        "name": vm.name,
        "state": state,
        "detail": detail,
        "pid": pid,
        "viewer": vm.viewer_pid(),
        "vnc": vm.vnc_info(),
        "uptime": vm.uptime(),
        "dir": vm.dir,
        "unit": vm.unit() or None,
        "reason": vm.reason(),
        **vm.summary(),
        "model_key": vm.has_model_key(),
        "identity": vm.identity_dir,
        "ssh": ssh_commands(vm),
        "inspector": inspector_info(vm),
        "gpu": gpu.measure([(pid, vm.config.get("layers"))])[0],
    }


def cmd_info(lib, a):
    from .. import gpu

    data = info_data(lib.vm(a.name))
    text = dict(data, gpu=gpu.label(data["gpu"]))
    return emit(a, data, "\n".join(kv_lines(text)))


def ssh_commands(vm):
    """How to reach the VM over SSH at its address (127.0.1.<slot>; listen's when that names one
    address: VM.service_host), for the layers it has: root
    (a layer that forwards the guest's 22, layer.toml: rootssh, or Homebrew Channel's SSH with
    hbchannel) with its client key and prisoner (devmode)
    with its developer key, both pinned to its known_hosts; for devmode also the key server and
    how to run the ares tools against it (beanjar ares-TOOL, ares.py)."""
    from .. import layermeta

    ident = vm.identity_dir
    layers = vm.config.get("layers") or []
    addr = vm.service_host()  # its address, or listen's when that is one address
    pin = (
        f"-o UserKnownHostsFile={shlex.quote(os.path.join(ident, keys.KNOWN_HOSTS))} "
        "-o StrictHostKeyChecking=yes"
    )
    out = {}
    if addr is None:
        return out
    port = layermeta.root_port(layers, vm.config.get("SSH_PORT"))
    if port:
        out["root"] = (
            f"ssh -p {port} -i {shlex.quote(os.path.join(ident, keys.CLIENT))} {pin} root@{addr}"
        )
    if "devmode" in layers:
        out["devmode"] = (
            f"ssh -p {keys.DEVMODE_PORT} -i {shlex.quote(os.path.join(ident, keys.DEVMODE))} {pin} "
            f"prisoner@{addr}"
        )
        out["devmode_key_server"] = f"http://{addr}:{keys.KEYSERVER_PORT}/webos_rsa"
        out["ares"] = f"beanjar ares-TOOL -d {shlex.quote(vm.name)} [ARGS...]"
    return out


def inspector_info(vm):
    """The traffic inspector of a running mode=mitm VM: its address (with link lan the LAN one, and
    local_url, the VM's own address, for this host, which cannot reach the LAN one) and the file
    with its password. The password itself stays in that owner-only file: status output gets
    pasted into logs and issues, and the manager's inspector panel shows the password where it is
    needed."""
    from .. import inspector

    state = inspector.snapshot(vm)
    if not state.url:
        return None
    return {
        "url": state.url,
        **({"local_url": state.local_url} if state.local_url else {}),
        "password_file": state.password_file or None,
    }


def create_arguments(p, epk=False):
    """create's arguments; with epk, --epk too (newvm.py, the GUI's New VM)."""
    p.add_argument("name")
    src = p.add_mutually_exclusive_group() if epk else p
    src.add_argument(
        "--firmware",
        metavar="NAME|PATH",
        help="the firmware: a name in the host's firmware collections (beanjar host config's "
        "firmware_dirs), or a directory of extracted paks (any value with a '/'); used in place, "
        "never written (it needs a manifest.toml), unless --import",
    )
    if epk:
        src.add_argument(
            "--epk", help="a firmware file to extract into the VM's firmware/ (epk2extract)"
        )
    p.add_argument(
        "--import",
        dest="import_",
        action="store_true",
        help="make --firmware the VM's own instead: <vm>/firmware, hard links on the same "
        "filesystem, else a copy",
    )
    p.add_argument(
        "--mem", type=int, help="MiB (default: the firmware's model's own, at each launch)"
    )
    p.add_argument("--smp", type=int, help="CPUs (default 4)")
    p.add_argument("--layers", help="comma-separated, highest priority first (default normal,gl)")
    p.add_argument(
        "--link",
        choices=vmsettings.LINKS,
        help="where the NIC is attached: nat (default), lan (mode mitm only for now) or off",
    )
    p.add_argument(
        "--mode", choices=vmsettings.MODES, help="mitm (default) or direct; ignored with --link=off"
    )
    p.add_argument("--display", choices=vmsettings.DISPLAYS, help="default dbus")
    p.add_argument(
        "--audio", choices=vmsettings.AUDIOS, help="default dbus with the dbus display, else none"
    )
    p.add_argument(
        "--tag",
        action="append",
        metavar="TAG",
        help="a tag (letters, digits, dashes; repeatable): kept in vm.toml, copied by clone, "
        "listed by list --tag (conventions: scratch, keep)",
    )


def reason_options(s, text=None):
    """start's --reason TEXT (text) and --by LABEL, reason's --by."""
    if text:
        s.add_argument(
            text,
            dest="reason",
            metavar="TEXT",
            help="why it runs, shown by list, info and the web viewer until the VM's unit ends; "
            "stop refuses it without --force (beanjar reason changes it)",
        )
    s.add_argument(
        "--by",
        metavar="LABEL",
        help="who set the reason (an agent, a script), recorded with the caller's user and the time",
    )


def register_lifecycle(cli):
    sub, command, vm_name = cli.sub, cli.command, cli.vm_name
    s = command(sub, "list", "VMs and their state, one per line", cmd_list)
    s.add_argument(
        "--all",
        action="store_true",
        help="also every VM running outside this library: other "
        "libraries' (development VMs), the regress harness's (the units in beanjar.slice); "
        "asked of the hosts too",
    )
    where = s.add_mutually_exclusive_group()
    where.add_argument(
        "--local", action="store_true", help="only this machine's VMs, no host asked"
    )
    where.add_argument("--host", metavar="HOST", help="only this host's VMs (local: this machine)")
    s.add_argument(
        "--tag",
        action="append",
        metavar="TAG",
        help="only the VMs with this tag (repeatable: with all of them)",
    )

    s = command(sub, "create", "define a VM on extracted firmware", cmd_create)
    create_arguments(s)

    s = command(
        sub,
        "start",
        "boot a VM (beside others, if the host has the memory and fewer than "
        "max_running VMs run: beanjar host config); returns at the initramfs shell",
        cmd_start,
    )
    vm_name(s)
    s.add_argument("args", nargs="*", help="extra kernel command line words")
    reason_options(s, "--reason")

    s = command(
        sub,
        "reason",
        "why a running VM runs: show it, set it to TEXT, or clear it with an empty TEXT. It lasts "
        "as long as the VM's systemd unit (gone when the VM stops, crashes or the host reboots); "
        "stop refuses a VM that has one unless --force",
        cmd_reason,
    )
    vm_name(s)
    s.add_argument("text", nargs="?", metavar="TEXT", help="the reason; '' clears it")
    reason_options(s)

    s = command(
        sub,
        "stop",
        "shut a VM down: its logs saved, then the power-off signal (webOS powers the set off and "
        "QEMU exits); a guest that cannot take it now (still starting, standby), whose webOS "
        f"refuses it, or that has not powered off {poweroff.POWER_OFF_TIMEOUT} s after it gets its "
        "disks synced and QEMU stopped instead",
        cmd_stop,
    )
    vm_name(s)
    how = s.add_mutually_exclusive_group()
    how.add_argument(
        "--unplug",
        action="store_true",
        help="kill QEMU at once, as a set losing its power: no logs, no sync; what webOS has not "
        "written to its disks is lost",
    )
    how.add_argument(
        "--no-fallback",
        action="store_true",
        help="the power-off signal or nothing: fail, leaving the VM running, where stop would "
        "sync and stop it instead (a later stop waits again for a signal sent)",
    )
    s.add_argument(
        "--force",
        action="store_true",
        help="stop it even if it runs for a reason (beanjar reason), which stop otherwise prints "
        "and refuses",
    )


def register_vm(cli):
    sub, command, vm_name = cli.sub, cli.command, cli.vm_name
    s = command(sub, "info", "one VM: its state, settings, addresses and SSH commands", cmd_info)
    vm_name(s, "info")

    s = command(
        sub,
        "tag",
        "a VM's tags: show them, add +TAG (or TAG), remove -TAG; kept in its vm.toml, whether it "
        "runs or not",
        cmd_tag,
        usage="beanjar tag NAME [+TAG | -TAG ...] [--json]",
    )
    vm_name(s)
    s.add_argument("changes", nargs=argparse.REMAINDER, metavar="+TAG|-TAG")

    s = command(sub, "delete", "remove a VM that is off, with its state", cmd_delete)
    vm_name(s)

    s = command(
        sub,
        "rename",
        "rename a VM that is off, in its library on its host: the same TV (its identity, slot and "
        "address, settings, state and logs kept); with link lan its DHCP hostname follows at its "
        "next start",
        cmd_rename,
        usage="beanjar rename NAME NEW [--json]",
    )
    vm_name(s, "rename")
    s.add_argument("new", metavar="NEW", help="the new name, a bare one (the VM stays on its host)")

    s = command(
        sub,
        "prepare",
        "work out what a VM's start needs from its firmware and board again (the partition map, "
        "the new EEPROM chip): create and a settings change do it, and a start whose data is "
        "missing or stale does it first",
        cmd_prepare,
    )
    vm_name(s)
    s.add_argument(
        "--if-stale",
        action="store_true",
        help="only when what it has is missing or stale (the GUI's Settings, after a change)",
    )

    s = command(sub, "wipe", "blank a VM's state disks (all of them without --part)", cmd_wipe)
    vm_name(s)
    s.add_argument(
        "--part",
        action="append",
        help="one of the VM's state disks (db8, cmn_data, ...: its firmware target's); repeatable",
    )
