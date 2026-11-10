# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Where a test's beanjar-vnc listens on TCP: an address of 127.0.0.0/8 of this test run's own,
and ports on it handed out in turn. Tests run in parallel; picking a free port by binding port 0
and closing it, then starting beanjar-vnc on it, leaves a moment in which another test can take
that port. On an address no other run uses, nothing else listens, so the ports are ours. The
address avoids 127.0.0.x and 127.0.1.x (beanjar's VMs listen on 127.0.1.N), and the ports start
at 20000, away from 5900 and up, which a VNC server on all addresses (wayvnc on the Pi) holds."""

import itertools
import os

_b = os.urandom(3)
ADDRESS = f"127.{2 + _b[0] % 250}.{_b[1]}.{1 + _b[2] % 254}"
_ports = itertools.count(20000)


def port():
    """A port on ADDRESS no earlier call returned."""
    return next(_ports)
