# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""A VM's identity as a TV: identity renew, clone, and its secure-store keys (key)."""

import os

from .. import remote
from ..core import Error
from .common import Usage, emit, progress, warn


def cmd_identity_renew(lib, a):
    vm, mac, serial = lib.renew_identity(a.name, out=progress)
    has = vm.has_model_key()
    return emit(
        a,
        {"name": vm.name, "mac": mac, "serial": serial, "model_key": has},
        f"{vm.name}: a new TV (mac {mac}, serial {serial}, new SSH and developer keys, a new VNC "
        f"password, state wiped; model key {'kept' if has else 'none'})",
    )


def cmd_clone(lib, a):
    from .. import clone

    r = clone.clone(lib, a.source, a.dest, out=progress, warn=warn)
    return emit(
        a,
        r,
        f"{r['source']} copied to {r['host'] or remote.LOCAL}/{r['name']}: mac {r['mac']}, serial "
        f"{r['serial']}, new SSH and developer keys and VNC password; settings, state and "
        "secure-store keys kept",
    )


def cmd_key_model(lib, a):
    """A VM's model key: whether it has one (show), install it from FILE, or clear it."""
    verb = a.verb or "show"
    if verb == "install":
        if not a.file:
            raise Usage(f"beanjar key model {a.name} install FILE")
        vm = lib.set_model_key(a.name, a.file)
    elif a.file is not None:
        raise Usage(f"only beanjar key model {a.name} install takes a FILE")
    elif verb == "clear":
        vm = lib.set_model_key(a.name, None)
    else:
        vm = lib.vm(a.name)
    has = vm.has_model_key()
    from .. import sestore

    try:
        missing, _items = sestore.load(vm.identity_dir)
    except sestore.Error as error:
        raise Error(str(error)) from None
    source = os.path.join(vm.identity_dir, sestore.CONFIG)
    return emit(
        a,
        {"name": vm.name, "model_key": has, "source": source if has else None, "missing": missing},
        f"{vm.name}: "
        + (
            f"provisioned model key ({source}; value omitted)"
            if has
            else "no model key: "
            + ("missing policy is error" if missing == "error" else "the built-in model secret")
        ),
    )


def cmd_key_list(lib, a):
    """The item names in a VM's identity/sestore.toml, never their values."""
    from .. import sestore

    vm = lib.vm(a.name)
    try:
        missing, items = sestore.load(vm.identity_dir)
    except sestore.Error as error:
        raise Error(str(error)) from None
    names = sorted(items)
    return emit(
        a,
        {"name": vm.name, "items": names, "missing": missing},
        "\n".join(names) if names else f"{vm.name}: no items in {sestore.CONFIG}",
    )


def register(cli):
    sub, command, group, vm_name = cli.sub, cli.command, cli.group, cli.vm_name
    g = group("identity", "a VM's identity as a TV")
    s = command(
        g,
        "renew",
        "make a VM that is off another TV: its state disks wiped, a new MAC and "
        "serial, new SSH and developer keys, a new VNC password (the model key and the library's "
        "CA kept)",
        cmd_identity_renew,
    )
    vm_name(s)

    s = command(
        sub,
        "clone",
        "make DEST a copy of the VM SRC, which is off, as another TV: SRC's settings, state disks "
        "and secure-store keys (the model key) kept, a new MAC, serial, SSH and developer keys and "
        "VNC password as identity renew makes them, the state not wiped. Either may be on a host "
        "(HOST/NAME; DEST without one is this machine's), this machine relaying the copy; DEST's "
        "host needs SRC's firmware at the same path",
        cmd_clone,
        usage="beanjar clone SRC DEST [--json]",
    )
    s.add_argument(
        "source", metavar="SRC", help="the VM to copy: NAME or HOST/NAME; it must be off"
    )
    s.add_argument(
        "dest", metavar="DEST", help="the new VM: NAME (this machine), local/NAME or HOST/NAME"
    )

    g = group("key", "a VM's secure-store keys (identity/sestore.toml)")
    s = command(
        g,
        "model",
        "a VM's SDP model key: show whether it has one, install one from FILE (44 characters "
        "of base64 for 32 bytes), or clear it (then follow the missing policy); the value is never "
        "printed; install and clear while it is off",
        cmd_key_model,
        usage="beanjar key model NAME [show | install FILE | clear] [--json]",
    )
    vm_name(s, "keymodel")
    s.add_argument(
        "verb",
        nargs="?",
        choices=("show", "install", "clear"),
        metavar="VERB",
        help="show (the default), install or clear",
    )
    s.add_argument("file", nargs="?", metavar="FILE")
    s = command(
        g, "list", "the item names in the VM's sestore.toml, never their values", cmd_key_list
    )
    vm_name(s)
