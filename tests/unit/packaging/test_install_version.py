#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The install commands in README.md and docs/user/getting-started.md download the release of
packaging/VERSION: their version= lines name it, so the commit that bumps the version updates them
too (packaging/debian/README.md, "Built on GitHub")."""

import re
from pathlib import Path

import pytest

TOP = Path(__file__).resolve().parents[3]
DOCS = ["README.md", "docs/user/getting-started.md"]


@pytest.mark.parametrize("doc", DOCS)
def test_install_names_the_packaged_version(doc):
    version = (TOP / "packaging/VERSION").read_text().strip()
    found = re.findall(r"^version=(\S*)$", (TOP / doc).read_text(), re.MULTILINE)
    assert found, f"{doc} has no version= line in its install commands"
    assert found == [version] * len(found), (
        f"{doc}: version={found} in the install commands, packaging/VERSION is {version}"
    )
