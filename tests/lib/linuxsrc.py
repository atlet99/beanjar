# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The unpacked kernel source the guest kernel is built from (subprojects/linux.wrap's directory,
which the build fetches), or LINUX_SRC: what the kernel tests read the genuine code from."""

import os
from pathlib import Path

TOP = Path(__file__).resolve().parents[2]


def linux_source():
    """The kernel source tree; it may not exist (not fetched yet): callers skip then."""
    if os.environ.get("LINUX_SRC"):
        return Path(os.environ["LINUX_SRC"])
    for line in (TOP / "subprojects/linux.wrap").read_text().splitlines():
        if line.startswith("directory"):
            return TOP / "subprojects" / line.split("=", 1)[1].strip()
    raise ValueError("subprojects/linux.wrap names no directory")
