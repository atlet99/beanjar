# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The command line's parser: the global options, and each command group's own commands and
options (each module's register functions, called in the order `beanjar --help` lists them)."""

import argparse

from .. import core
from . import (
    devices,
    display,
    identity,
    mitm,
    remote_control,
    sensors,
    settings,
    vms,
)
from .common import parser_class


class Tree:
    """What a module's register(cli) adds its commands with: sub, the top-level subparsers;
    command, group and vm_name."""

    def __init__(self, sub, common):
        self.sub, self.common = sub, common

    def command(self, group, name, help, func, usage=None, **kw):
        s = group.add_parser(
            name, help=help, description=help, usage=usage, parents=[self.common], **kw
        )
        s.set_defaults(func=func)
        return s

    def group(self, name, help, under=None, dest="verb_group", metavar="VERB"):
        g = (under or self.sub).add_parser(name, help=help, description=help)
        return g.add_subparsers(dest=dest, required=True, metavar=metavar)

    @staticmethod
    def vm_name(s, remote="forward"):
        # NAME is the VM here, or HOST/NAME on a configured host (forward.remote_command)
        s.add_argument(
            "name", metavar="NAME", help="the VM: NAME, or HOST/NAME for a remote host's"
        )
        s.set_defaults(remote=remote)


def register_ares(cli):
    """ares-TOOL, for the help: __init__.ares_main runs it before the parser."""
    sub = cli.sub
    s = sub.add_parser(
        "ares-TOOL",
        help="run a webOS SDK tool (ares-install, ares-shell, ares-launch, ...) against the VM "
        "NAME alone, ARGS passed on: beanjar ares-TOOL [--root] -d NAME [ARGS...]",
        description="Run the webOS SDK tool ares-TOOL (the Rust ares-cli or the official CLI, "
        "from PATH or ~/.cargo/bin) against the running VM NAME (it needs the devmode layer), "
        "which is then the tool's only and default device, the developer-mode user prisoner; "
        "with --root it is root over the VM's root SSH instead (it needs a layer that forwards "
        "the guest's port 22: rootssh, or hbchannel with devmode) with the VM's client key. "
        "-d NAME and --root are beanjar's, the other arguments are the tool's (its own help: "
        "beanjar ares-TOOL -d NAME --help). The exit status is the tool's. The tool always runs "
        "on this machine: a VM on a remote host (HOST/NAME) is reached through an ssh port "
        "forward to it, made for the command.",
        usage="beanjar ares-TOOL [--root] -d NAME [ARGS...]",
    )
    s.add_argument("args", nargs="*", metavar="ARGS", help="the tool's arguments")


REGISTER = (
    vms.register_lifecycle,
    display.register_pictures,
    vms.register_vm,
    settings.register_vm_settings,
    display.register,
    remote_control.register,
    identity.register,
    settings.register_config,
    register_ares,
    sensors.register,
    devices.register,
    mitm.register,
)


def build():
    """The command line's parser."""
    Parser = parser_class()  # noqa: N806 (a class)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument(
        "--json",
        action="store_true",
        help="print the result as JSON on stdout (an error as one JSON object on stderr)",
    )
    p = Parser(
        prog="beanjar",
        description="Beanjar: webOS TV firmware in QEMU VMs (host/core/README.md)",
        epilog="Every command takes --json. Exit status: 0 done, 1 the operation failed, 2 a "
        "usage error. Progress and warnings go to stderr. A VM's setting keys: beanjar settings "
        "--help.",
    )
    p.add_argument(
        "--library",
        help=f"library directory (default $BEANJAR_LIBRARY or {core.default_library()})",
    )
    p.add_argument("--repo", help="the Beanjar checkout (stored in the library's config.toml)")
    sub = p.add_subparsers(dest="cmd", required=True, metavar="COMMAND")
    cli = Tree(sub, common)
    for register in REGISTER:
        register(cli)
    return p
