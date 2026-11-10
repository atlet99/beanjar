# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-guest compatibility, checked by what the guest payload has rather than by version numbers
(docs/dev/architecture/build-and-boot.md, "Host and guest compatibility").

Before a VM is created or started, components() makes sure the payload has every always-used
component of the firmware's target; before it boots, module_params() makes sure every parameter the host
passes to a guest module (boot.conf's MODULE_PARAMS_*) is one that module's .ko declares in its
.modinfo. The versions (the host's and the guest payload's installation.toml) are information only:
they go into run/vm-info.toml's [beanjar] table for logs and messages, and nothing is refused by them.
"""

import json
import os
import tomllib

from . import targets

MODINFO_CACHE = "modinfo-params.json"


class Error(Exception):
    pass


def _version(path, read):
    try:
        value = read(path)
    except (OSError, ValueError):
        return "unknown"
    return value if isinstance(value, str) and value.strip() else "unknown"


def _toml_version(path):
    with open(path, "rb") as f:
        return tomllib.load(f).get("version")


def _text(path):
    with open(path) as f:
        return f.read().strip()


def host_version():
    """The running host's version (its installation.toml, or the checkout's packaging/VERSION), or
    "unknown"."""
    from . import paths

    tree = paths.TREE
    if tree.installed:
        return _version(os.path.join(tree.top, "installation.toml"), _toml_version)
    return _version(tree.src("packaging", "VERSION"), _text)


def guest_version(tree):
    """The guest payload's version (its installation.toml, or a checkout's build/guest-installation.toml),
    or "unknown": information only."""
    return _version(tree.build("guest-installation.toml"), _toml_version)


def run_info(tree):
    """The [beanjar] table of run/vm-info.toml: the versions of this launch, for logs and messages."""
    return {"host_version": host_version(), "guest_version": guest_version(tree)}


def components(tree, target, name):
    """Error unless the guest payload has every always-used component of the firmware target."""
    from . import guestlayers, share

    missing = []
    for layer in targets.always_layers(target):
        try:
            if guestlayers.resolve(tree, target, layer):
                continue
        except (ValueError, OSError) as e:
            raise Error(f"Cannot start '{name}': guest component {layer}: {e}") from None
        if share.resolve_layer(tree, target, layer)[0] is None:
            missing.append(layer)
    if missing:
        what = "beanjar-guest" if tree.installed else "the guest build (scripts/build.sh guest)"
        raise Error(
            f"Cannot start '{name}': the guest payload has no {', '.join(missing)} component for "
            f"{targets.name(target)}: install {what} with this firmware target"
        )


# ---------------------------------------------------------------- module parameters
def declared_params(ko):
    """The parameter names a kernel module declares: its .modinfo's parm= and parmtype= entries."""
    from elftools.elf.elffile import ELFFile

    with open(ko, "rb") as f:
        section = ELFFile(f).get_section_by_name(".modinfo")
        data = section.data() if section is not None else b""
    names = set()
    for entry in data.split(b"\0"):
        key, _, value = entry.partition(b"=")
        if key in (b"parm", b"parmtype") and b":" in value:
            names.add(value.split(b":", 1)[0].decode(errors="replace"))
    return names


def _cached_params(tree, ko):
    """declared_params(ko), cached by the file's path, size and mtime (tree's cache)."""
    st = os.stat(ko)
    key = [st.st_size, st.st_mtime_ns]
    path = tree.cache(MODINFO_CACHE)
    try:
        with open(path) as f:
            cache = json.load(f)
        if not isinstance(cache, dict):
            cache = {}
    except (OSError, ValueError):
        cache = {}
    hit = cache.get(ko)
    if isinstance(hit, dict) and hit.get("stat") == key and isinstance(hit.get("params"), list):
        return set(hit["params"])
    params = declared_params(ko)
    cache[ko] = {"stat": key, "params": sorted(params)}
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path + ".tmp", "w") as f:
            json.dump(cache, f)
        os.replace(path + ".tmp", path)
    except OSError:
        pass  # a cache only
    return params


def module_params(tree, params, name):
    """Error unless each guest module of params ({module: words}, what boot.conf's MODULE_PARAMS_*
    pass, targets.module_params) declares every parameter it is given: the guest's .ko, read where the
    payload has it (tree.build("<module>.ko"))."""
    for module, words in params.items():
        wanted = [w.split("=", 1)[0] for w in words]
        if not wanted:
            continue
        ko = tree.build(f"{module}.ko")
        try:
            declared = _cached_params(tree, ko)
        except FileNotFoundError:
            raise Error(
                f"Cannot start '{name}': the guest payload has no {ko}, whose parameters "
                f"{', '.join(wanted)} this host passes"
            ) from None
        except Exception as e:  # not an ELF file, a broken one
            raise Error(f"Cannot start '{name}': cannot read {ko}'s .modinfo: {e}") from None
        for param in wanted:
            if param not in declared:
                raise Error(
                    f"Cannot start '{name}': {module}.ko has no parameter {param}, which this host "
                    f"passes ({ko}): install a guest payload built with it, or a host that does not "
                    "pass it"
                )
