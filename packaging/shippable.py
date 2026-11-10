#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""What the build publishes for every VM and user (docs/dev/guides/build.md, "What may be distributed") holds
nothing of one install: no netproxy layer, no key, secret or authorized_keys file, no PEM private
key in any file. packaging/debian/build.py runs it on what it packages; the integration tier on what
the build published (tests/integration/packaging/test_published.py).

    packaging/shippable.py [PUBLISH_DIR]      (default build/)

Reads PUBLISH_DIR/initramfs.cpio.gz (newc, parsed here: no cpio needed) and what a package ships
of PUBLISH_DIR/components: the selected variants, as packaging/debian/build.py copies them, not
stale ones no target selects any more (meson/components.py's shipped()); of a staged package's
layers, all of it. Exit status 77 (skip) if there is no initramfs: the guest build has not
published one (the Pi, or a host-only checkout). Whether a file is one of the firmware's own is not
checked here: the paks are not in the checkout."""

import fnmatch
import gzip
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
BAD_NAMES = (
    "*.key",
    "*secret*",
    "authorized_keys",
    "known_hosts",
    "id_ed25519*",
    "id_rsa*",
    "*_rsa",
    "*host_key*",
    "*.pem",
    "ca.*",
    "nduid",
    "passphrase",
)
# Files named like that which are public and the same for everyone (still checked for a private key):
# Homebrew Channel's bundle of Mozilla's CA roots (curl's cacert), from its release IPK (guest/layers/hbchannel).
PUBLIC = ("*/usr/palm/services/org.webosbrew.hbchannel.service/certs/cacert-*.pem",)
PRIVATE = re.compile(
    rb"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----\r?\n(Proc-Type:[^\n]*\n)?[A-Za-z0-9+/=\n:,-]{40}"
)


def cpio_entries(path):
    """(name, mode, data) of each entry of a gzipped newc cpio."""
    with gzip.open(path, "rb") as f:
        raw = f.read()
    i = 0
    while i + 110 <= len(raw):
        if raw[i : i + 6] not in (b"070701", b"070702"):
            raise ValueError(f"{path}: no newc header at {i}")
        fields = [int(raw[i + 6 + 8 * k : i + 14 + 8 * k], 16) for k in range(13)]
        mode, size, namesize = fields[1], fields[6], fields[11]
        name = raw[i + 110 : i + 110 + namesize - 1].decode()
        i = (i + 110 + namesize + 3) & ~3
        data = raw[i : i + size]
        i = (i + size + 3) & ~3
        if name == "TRAILER!!!":
            return
        yield name, mode, data


def problems(name, data):
    out = []
    parts = name.strip("./").split("/")
    if parts[:2] == ["layers", "netproxy"]:
        out.append("the netproxy layer (the proxy's CA of one install)")
    base = parts[-1]
    public = any(fnmatch.fnmatch("/" + "/".join(parts), pat) for pat in PUBLIC)
    for pat in () if public else BAD_NAMES:
        if fnmatch.fnmatch(base, pat):
            out.append(f"a file named like a key or secret ({pat})")
            break
    if data and PRIVATE.search(data):
        out.append("a PEM private key in it")
    return out


def shipped(layers):
    """Every file under layers a package ships. A publish directory's components hold variants
    no target selects any more, which the package leaves out: there only what
    meson/components.py's shipped() names, the packager's own selection. A staged package's
    layers: all of it."""
    if os.path.basename(layers) != "components":
        roots = [layers]
    else:
        spec = importlib.util.spec_from_file_location(
            "components", os.path.join(TOP, "meson/components.py")
        )
        components = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(components)
        roots = [os.path.join(layers, p) for p in components.shipped(layers)]
    for root in roots:
        if not os.path.isdir(root) or os.path.islink(root):
            yield root
            continue
        for d, _dirs, files in os.walk(root):
            for f in files:
                yield os.path.join(d, f)


def main():
    top = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(TOP, "build"))
    cpio = os.path.join(top, "initramfs.cpio.gz")
    if not os.path.isfile(cpio):
        print(f"SKIP: no {cpio} (the guest build publishes it)")
        return 77
    bad, n = [], 0
    for name, mode, data in cpio_entries(cpio):
        n += 1
        bad += [
            f"{cpio}: {name}: {p}"
            for p in problems(name, data if mode & 0o170000 == 0o100000 else b"")
        ]
    print(f"{cpio}: {n} entries")
    layers = os.path.join(
        top, "components" if os.path.isdir(os.path.join(top, "components")) else "layers"
    )
    for d in [layers] + [
        os.path.join(layers, p) for p in (os.listdir(layers) if os.path.isdir(layers) else [])
    ]:
        if os.path.isdir(os.path.join(d, "netproxy")):  # guest/layers/<chip>-<release>/netproxy
            bad.append(f"{d}/netproxy: the netproxy layer is published")
    try:
        for path in shipped(layers):
            rel = os.path.relpath(path, top)
            data = b""
            if os.path.isfile(path) and not os.path.islink(path):
                with open(path, "rb") as fh:
                    data = fh.read()
            bad += [f"{rel}: {p}" for p in problems(rel, data)]
            n += 1
    except ValueError as e:  # a selection naming a missing or changed variant
        bad.append(str(e))
    print(f"{layers}: checked" if os.path.isdir(layers) else f"no {layers}")
    for line in bad:
        print(f"FAIL: {line}")
    print("PASS" if not bad else "FAIL")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
