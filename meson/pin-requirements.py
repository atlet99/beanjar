#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Add sha256 hashes to a pip requirements file (for pip install --require-hashes).

    meson/pin-requirements.py FILE

Each 'name==version' line (comments and blank lines are kept) gets the hashes of every file
PyPI has for that release, wheels for every platform and the sdist, so the same file installs
on the x86_64 workstation and on the aarch64 Pi. Existing '--hash' continuation lines are
replaced. Run it after changing a pin; the hashes come from PyPI's JSON API.
"""

import json
import re
import sys
import urllib.request


def hashes(name, version):
    url = f"https://pypi.org/pypi/{name}/{version}/json"
    with urllib.request.urlopen(url, timeout=60) as r:
        data = json.load(r)
    return sorted({f["digests"]["sha256"] for f in data["urls"]})


def main(path):
    lines = []
    with open(path) as f:
        text = f.read()
    # Drop existing hash continuations.
    text = re.sub(r" \\\n(\s+--hash=sha256:[0-9a-f]+(?: \\\n)?)+", "\n", text)
    for line in text.splitlines():
        m = re.match(r"^([A-Za-z0-9_.\-]+)==([^\s;\\]+)\s*$", line)
        if not m:
            lines.append(line)
            continue
        hs = hashes(m.group(1), m.group(2))
        if not hs:
            sys.exit(f"{line}: no files on PyPI")
        lines.append(line + " \\")
        lines += [
            f"    --hash=sha256:{h}" + (" \\" if i < len(hs) - 1 else "") for i, h in enumerate(hs)
        ]
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
