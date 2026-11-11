#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The guest build's firmware facts (host/core/beanjar/fwfacts.py; guest/targets/meson.build):

fwstub.py stub FACTS LIB OUT -- CC...     the stub library of firmware library LIB (a lib line's
                                          path), compiled with CC to OUT
fwstub.py questions OUT [QUESTION...]     write the build's questions ("KIND PATH") to OUT
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "host", "core"))
from beanjar import fwfacts  # noqa: E402

if __name__ == "__main__":
    sys.exit(fwfacts.stub_main())
