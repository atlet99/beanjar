# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Beanjar's versions and their host-guest contract (packaging/debian/README.md, "Versions and
compatibility"). The one implementation: packaging/identity.py loads this file too.

A version is X.Y.Z, or X.Y.Z+gitN.g<12 hex> for a build N commits after packaging/VERSION last
changed. Its contract is major.minor while the major is 0 (0.2), the major from 1.0 (1): a host and
a guest of the same contract work together."""

import re

VERSION = re.compile(
    r"(?P<base>(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*))"
    r"(\+git(?P<count>[1-9][0-9]*)\.g(?P<hash>[0-9a-f]{12}))?"
)


def valid(version):
    return isinstance(version, str) and VERSION.fullmatch(version) is not None


def contract(version):
    """'0.2' for 0.2.x, '1' for 1.x; ValueError for something that is not a version."""
    m = VERSION.fullmatch(version or "") if isinstance(version, str) else None
    if not m:
        raise ValueError(f"{version!r} is not a beanjar version")
    major, minor, _ = m["base"].split(".")
    return f"0.{minor}" if major == "0" else major


def next_contract(version):
    """The first version past version's contract: '0.3' for 0.2.x, '2' for 1.x."""
    current = contract(version)
    if current.startswith("0."):
        return f"0.{int(current[2:]) + 1}"
    return str(int(current) + 1)
