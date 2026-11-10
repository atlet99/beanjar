# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""What the guest sees of the host: a per-VM runtime tree, RUN_DIR/share, and nothing else.
The checkout, local/, identity/, the library's CA
and the firmware are not shared; the files the guest needs are linked (or copied) into the tree.

  RUN_DIR/share/vm/    9p "vm", read-only: the per-VM layer (qemu.guest_layer), mounted by the
                       initramfs at /run/vm only, not bound into webOS
  RUN_DIR/share/host/  9p "host", read-only: /host in the initramfs, /mnt/host in webOS. Made afresh
                       at each launch (assemble):
      boot.conf          the boot manifest (targets.boot_conf): the target's paks, layers, state disks
                         and firmware paths, which guest/initramfs/rootfs/init carries out
      layer-sources      "<name> <source>" per line, which init logs beside each layer. The
                         layers themselves are not in the share: each is a read-only disk
                         (layerimage.py, qemu.plan) of resolve_layers' directories
      binds.conf         the active lines of guest/layers/binds.conf; each source at its path below
      guest/scripts/save-logs.sh, guest/scripts/save-device-counters.sh, guest/scripts/check-m1.sh,
      guest/scripts/time-boot.sh   (BASE)
      <path>             what `host/scripts/vm-launcher put PATH` adds while the VM runs (PUT_ALLOWED)
      out/               the mount point of "out"
  RUN_DIR/share/out/   9p "out", read-write: /host/out, /mnt/host/out in webOS. The guest's
                       output: logs/<name>/ (guest/scripts/save-logs.sh, finished when it has .done),
                       which the launcher moves to SAVED_LOG_DIR (harvest), and the tests'
                       results. Kept across launches.

Files are hard links where RUN_DIR is on the checkout's filesystem, copies elsewhere. The host share is read-only, so the guest cannot write through a link;
the host side never writes into a linked file either (put unlinks first)."""

import errno
import os
import re
import shutil
import stat
from typing import NamedTuple

from . import layermeta, targets

# The guest-side scripts every VM has (the launcher's save-logs.sh at stop, time-boot.sh, check-m1.sh).
BASE = (
    "guest/scripts/save-logs.sh",
    "guest/scripts/save-device-counters.sh",
    "guest/scripts/check-m1.sh",
    "guest/scripts/time-boot.sh",
)
# What `put` and binds.conf may take from the checkout: the test scripts and tools (the gl layer's
# too), and the guest build's test programs and layers. A directory entry ends in "/". Not local/,
# not the firmware or what is made from it (build/firmware-cache,
# build/tcg-perf/io.ext4), not the checkout VM's state or logs.
PUT_ALLOWED = (
    "tests/",
    "guest/scripts/",
    "guest/layers/gl/test/",
    "build/tests/",
    "build/layers/",
    "build/lgemu.ko",
    "build/mstar-utopia.ko",
    "build/lg115x.ko",
    "build/tcg-perf/perf-arm",
    "build/tcg-perf/io-perf",
    "build/devmode-tests/native-test",
)
LAYER_NAME = re.compile(r"[A-Za-z0-9_][A-Za-z0-9._-]*")


class Error(Exception):
    pass


def dirs(run_dir):
    """(share, vm, host, out) of a runtime directory."""
    share = os.path.join(run_dir, "share")
    return share, os.path.join(share, "vm"), os.path.join(share, "host"), os.path.join(share, "out")


def allowed(rel):
    """Whether a checkout-relative path may be put into the host share."""
    if rel.startswith("/") or ".." in rel.split("/") or rel in (".", ""):
        return False
    return any(
        rel == a.rstrip("/") or rel.startswith(a if a.endswith("/") else a + "/")
        for a in PUT_ALLOWED
    )


def _remove(path):
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path)
    elif os.path.lexists(path):
        os.unlink(path)


def _link_file(src, dst):
    """dst as a hard link to src (a copy across filesystems), or the same symlink. An existing dst
    is unlinked first, never written into (it may be a link to a checkout file)."""
    _remove(dst)
    if os.path.islink(src):
        os.symlink(os.readlink(src), dst)
        return
    try:
        os.link(src, dst)
    except OSError as e:
        if e.errno not in (errno.EXDEV, errno.EPERM, errno.EMLINK, errno.EACCES):
            raise
        shutil.copy2(src, dst)


def _mkdirs(path, mode=0o755):
    if not os.path.isdir(path):
        _mkdirs(os.path.dirname(path), mode)
        os.mkdir(path)
        os.chmod(path, mode)


def _link_tree(src, dst, skip=()):
    """The tree src at dst: directories made with src's modes, files linked, symlinks as they are
    (the guest resolves them inside its own view); skip: names at src's top left out."""
    _remove(dst)
    os.mkdir(dst)
    os.chmod(dst, stat.S_IMODE(os.stat(src).st_mode))
    for root, subdirs, files in os.walk(src):
        out = os.path.join(dst, os.path.relpath(root, src))
        for d in list(subdirs):
            s = os.path.join(root, d)
            if os.path.islink(s):
                os.symlink(os.readlink(s), os.path.join(out, d))
                subdirs.remove(d)
            else:
                os.mkdir(os.path.join(out, d))
                os.chmod(os.path.join(out, d), stat.S_IMODE(os.stat(s).st_mode))
        for f in files:
            if root == src and f in skip:
                continue
            _link_file(os.path.join(root, f), os.path.join(out, f))


def _checkout_rel(tree, path):
    """path (absolute, or relative to the checkout) as a checkout-relative path that may be put,
    checked before and after resolving symlinks; Error otherwise."""
    if tree.installed:
        root = os.path.realpath(tree.src("guest"))
        rel = (
            "guest/" + os.path.relpath(path, root)
            if os.path.isabs(path)
            else os.path.normpath(path)
        )
        full = tree.src(rel)
        if not allowed(rel) or os.path.commonpath((root, os.path.realpath(full))) != root:
            raise Error(f"{path}: not a packaged guest runtime file")
        if not os.path.lexists(full):
            raise Error(f"{path}: no such packaged guest file")
        return rel, full
    top = os.path.realpath(tree.top)
    full = os.path.abspath(path if os.path.isabs(path) else os.path.join(tree.top, path))
    rel = os.path.relpath(full, tree.top)
    real = os.path.relpath(os.path.realpath(full), top)
    if not (allowed(rel) and allowed(real)):
        raise Error(
            f"{path}: not something the guest may have (only {', '.join(PUT_ALLOWED)} of the checkout)"
        )
    if not os.path.lexists(full):
        raise Error(f"{path}: no such file in {tree.top}")
    return rel, full


def put(tree, host_dir, paths):
    """Link each of paths (files or directories of the checkout, PUT_ALLOWED) into host_dir at its
    checkout-relative path; returns those paths."""
    if not os.path.isdir(host_dir):
        raise Error(f"no {host_dir}: the VM has not been started")
    done = []
    for path in paths:
        rel, full = _checkout_rel(tree, path)
        dst = os.path.join(host_dir, rel)
        _mkdirs(os.path.dirname(dst))
        if os.path.isdir(full) and not os.path.islink(full):
            _link_tree(full, dst)
        else:
            _link_file(full, dst)
        done.append(rel)
    return done


def resolve_layer(tree, target, name):
    """(directory, checkout-relative source) of a layer for the firmware target (chip, release), or
    (None, why). The always-used layers are only ever generated (guest/layers/soc and guest/layers/webos hold
    one source directory per chip or release, not a layer)."""
    built, src = tree.layer(target, name), tree.src("guest", "layers", name)
    if os.path.isdir(built):
        return built, os.path.relpath(built, tree.top)
    if (
        os.path.isdir(src)
        and (not tree.installed or name == "normal")
        and name not in targets.ALWAYS_LAYERS
        and not os.path.isfile(os.path.join(src, "meson.build"))
    ):
        return src, os.path.relpath(src, tree.top)
    if os.path.isdir(src):
        return None, f"not built (no {os.path.relpath(built, tree.top)})"
    return None, "not in the checkout"


def layer_names(layers, target):
    """LAYERS (highest priority first; NAME or NAME@VERSION) and then the target's always-used layers,
    each layer once, as init stacks them; the reserved name vm and names that are not plain names
    left out."""
    out = []
    for n in list(layers) + list(targets.always_layers(target)):
        name, version = layermeta.split(n or "")
        if (
            name
            and name != targets.VM_LAYER
            and LAYER_NAME.fullmatch(name)
            and (version is None or layermeta.VERSION.fullmatch(version))
            and name not in [layermeta.base(o) for o in out]
        ):
            out.append(n)
    return out


class Layer(NamedTuple):
    """A layer the guest stacks: its name in boot.conf, its directory, that directory relative to
    the checkout (layer-sources), and the names at its top left out of its image."""

    name: str
    src: str
    rel: str
    skip: tuple = ()


def resolve_layers(tree, target, layers):
    """(layers, missing) for LAYERS (highest priority first; NAME@VERSION picks a versioned layer's
    version, plain NAME its default) and the firmware target's always-used layers: the Layers,
    highest first, and the optional layers the host does not have, as notes. Error if an
    always-used layer is not built, or for a version the layer does not have."""
    from . import guestlayers

    out, missing = [], []
    meta = None
    for entry in layer_names(layers, target):
        name, version = layermeta.split(entry)
        if meta is None:
            meta = layermeta.load(tree)
        versions = meta[name].versions if name in meta else None
        if version is not None and versions is None:
            raise Error(f"layer {name} has no versions: name it without @{version}")
        if versions is not None:
            # a layer built once per version (layermeta.py): only its versions' components; the
            # guest knows it by its name
            version = version or versions.default
            if version not in versions.available:
                raise Error(
                    f"layer {name} has no version {version!r}; its versions: "
                    f"{layermeta.version_text(meta[name])}"
                )
            try:
                src = guestlayers.select(
                    tree.build("components"), target, guestlayers.versioned(name, version)
                )
            except (ValueError, OSError) as exc:
                raise Error(str(exc)) from exc
            if src is None:
                missing.append(f"{name}@{version} (not built for {targets.name(target)})")
            else:
                out.append(Layer(name, src, os.path.relpath(src, tree.top)))
            continue
        try:
            components = guestlayers.resolve(tree, target, name)
        except (ValueError, OSError) as exc:
            raise Error(str(exc)) from exc
        if components:
            out += [Layer(alias, src, os.path.relpath(src, tree.top)) for alias, src in components]
            continue
        src, rel = resolve_layer(tree, target, name)
        if src is None:
            if name in targets.always_layers(target):
                raise Error(
                    f"layer {name} of {targets.name(target)}: {rel} (run scripts/build.sh guest)"
                )
            missing.append(f"{name} ({rel})")
            continue
        # a source layer's own layer.toml (layermeta.py) is the host's, not a file of the guest's /
        out.append(Layer(name, src, rel, (layermeta.FILE,)))
    return out, missing


def assemble(
    tree,
    target,
    run_dir,
    layers,
    say,
    derived=None,
    part_nodes=(),
    vm=None,
    files=None,
    fw=None,
    broadcast=None,
):
    """Make RUN_DIR/share/host afresh and RUN_DIR/share/out if it is missing (the module's doc), with
    the boot.conf of the firmware target (chip, release) and its layers (resolve_layers' Layers,
    highest first; derived: the firmware's own module parameters, targets.derived_params;
    part_nodes: the eMMC partitions' nodes, qemu.part_nodes; vm: the VM's own module parameters,
    targets.boot_conf's; files: {name: path}, the files made from the firmware that the modules read
    as /host/<name>, targets.derived_files; fw, broadcast: the firmware directory and the board's
    broadcast standard, targets.boot_conf's)."""
    _share, _vm, host, out = dirs(run_dir)
    _remove(host)
    _mkdirs(os.path.join(host, "out"))
    _mkdirs(out)
    put(tree, host, BASE)
    for name, path in (files or {}).items():
        shutil.copyfile(path, os.path.join(host, name))
    with open(os.path.join(host, "layer-sources"), "w") as f:
        f.write("".join(f"{layer.name} {layer.rel}\n" for layer in layers))
    with open(os.path.join(host, "boot.conf"), "w") as f:
        f.write(
            targets.boot_conf(
                target, [layer.name for layer in layers], derived, part_nodes, vm, fw, broadcast
            )
        )
    binds = []
    conf = tree.src("guest", "layers", "binds.conf")
    if os.path.isfile(conf):
        with open(conf) as f:
            for line in f:
                words = line.split()
                if not words or words[0].startswith("#"):
                    continue
                if len(words) != 2:
                    say(f"binds.conf: not '<source> <target>': {line.strip()}")
                    continue
                try:
                    put(tree, host, [words[0]])
                except Error as e:
                    say(f"binds.conf: {e}")
                    continue
                binds.append(f"{os.path.normpath(words[0])} {words[1]}\n")
    if binds:
        with open(os.path.join(host, "binds.conf"), "w") as f:
            f.write("".join(binds))


def _free(dest):
    n, d = 1, dest
    while os.path.lexists(d):
        n += 1
        d = f"{dest}.{n}"
    return d


def save(log_dir, saved):
    """Move one finished log directory of the guest's out/logs into saved (a name taken there gets
    .2, .3 ...); returns where it went."""
    os.makedirs(saved, exist_ok=True)
    dest = _free(os.path.join(saved, os.path.basename(log_dir)))
    shutil.move(log_dir, dest)
    return dest


def harvest(run_dir, saved):
    """Move every finished (.done) log directory of RUN_DIR/share/out/logs into saved."""
    logs = os.path.join(dirs(run_dir)[3], "logs")
    try:
        names = sorted(os.listdir(logs))
    except FileNotFoundError:
        return []
    return [
        save(os.path.join(logs, n), saved)
        for n in names
        if os.path.isfile(os.path.join(logs, n, ".done"))
    ]
