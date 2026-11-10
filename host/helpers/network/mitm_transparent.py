# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""mitmproxy addon for NET=mitm: transparent mode without netfilter.

The NET=mitm namespace (host/core/beanjar/netns.py) delivers the guest's tcp/80 and tcp/443
to mitmweb by policy routing alone: a rule sends those packets from the TAP to a routing table
whose only route makes every destination local ("AnyIP"), and mitmweb listens on 0.0.0.0:80 and
0.0.0.0:443 itself. No NAT happens, so an accepted connection's own local address is the
destination the guest dialled. Stock mitmproxy's transparent mode needs two things changed for
that:

- The listening sockets get IP_TRANSPARENT. The packets are delivered locally, but without it the
  kernel will not answer from an address it does not own, so the guest's SYN goes unanswered.
- The original destination is getsockname(), not SO_ORIGINAL_DST. That socket option reads the
  conntrack NAT entry, and with no NAT it fails ("Request destination unknown").

Both reach into mitmproxy's internals: mitmproxy.platform.original_addr, which the transparent
server looks up at every connection, and the listening sockets, which only the private
Proxyserver.servers[*]._servers[*].sockets expose. Those are the ones of mitmproxy 12.2.3 (unchanged since 11.0.2), the
version host/helpers/network/requirements.txt pins. Any other version stops mitmweb at load with an error
naming the pin, rather than running with connections that silently hang: move the pin only after
checking these attributes again.

One more thing no NAT changes: a destination that is one of the namespace's own addresses would be
delivered straight back to the proxy, which would dial itself for ever. server_connect refuses it.
"""

import errno
import ipaddress
import logging
import os
import socket
import sys

import mitmproxy.platform
import mitmproxy.version
from mitmproxy import ctx
from mitmproxy.proxy import mode_servers, mode_specs

sys.dont_write_bytecode = True  # no __pycache__ in the source tree

PIN = "12.2.3"


def check_version(version=mitmproxy.version.VERSION):
    """Raise unless this is the mitmproxy the private attributes below were checked against."""
    where = "host/helpers/network/mitm_transparent.py"
    if version != PIN:
        raise RuntimeError(
            f"{where} needs mitmproxy {PIN}, the version host/helpers/network/requirements.txt pins; "
            f"this is {version}. It uses mitmproxy internals (platform.original_addr, "
            f"the listening sockets of the transparent servers): check them before moving the pin, "
            f"or rebuild build/mitmproxy-venv from requirements.txt"
        )
    missing = [
        name
        for name, ok in (
            ("mitmproxy.platform.original_addr", hasattr(mitmproxy.platform, "original_addr")),
            (
                "mitmproxy.proxy.mode_servers.platform",
                getattr(mode_servers, "platform", None) is mitmproxy.platform,
            ),
            (
                "AsyncioServerInstance._servers",
                "_servers" in getattr(mode_servers.AsyncioServerInstance, "__annotations__", {}),
            ),
        )
        if not ok
    ]
    if missing:
        raise RuntimeError(
            f"{where}: mitmproxy {version} lacks {', '.join(missing)}; see requirements.txt"
        )


def original_addr(sock):
    """The destination the guest dialled: with AnyIP routing and no NAT, the socket's own address."""
    return sock.getsockname()[:2]


def is_local(host):
    """True if host is an address of this network namespace (bind() accepts only local addresses)."""
    try:
        addr = ipaddress.ip_address(host)
    except ValueError:
        return False
    with socket.socket(
        socket.AF_INET if addr.version == 4 else socket.AF_INET6, socket.SOCK_STREAM
    ) as s:
        try:
            s.bind((str(addr), 0))
            return True
        except OSError as e:
            if e.errno == errno.EADDRNOTAVAIL:
                return False
            raise


class Transparent:
    def load(self, loader):
        check_version()
        mitmproxy.platform.original_addr = original_addr

    def running(self):
        ps = ctx.master.addons.get("proxyserver")
        ps.servers.changed.connect(self.apply)
        n = self.apply()
        if not n:
            raise RuntimeError(
                "mitm_transparent: no listening transparent-mode socket to set IP_TRANSPARENT on "
                "(mitmweb needs --mode transparent@PORT)"
            )
        ready = os.environ.get("NETNS_MITM_READY")  # netns.py waits for it before starting QEMU
        if ready:
            with open(ready, "w") as f:
                f.write(f"{n}\n")

    def apply(self):
        """Set IP_TRANSPARENT on every listening socket of a transparent-mode server; the count."""
        n = 0
        for server in ctx.master.addons.get("proxyserver").servers:
            if not isinstance(server.mode, mode_specs.TransparentMode):
                continue
            for s in server._servers:
                for sock in s.sockets:
                    sock.setsockopt(socket.SOL_IP, socket.IP_TRANSPARENT, 1)
                    n += 1
        return n

    def server_connect(self, data):
        host, port = data.server.address[:2]
        if is_local(host):
            data.server.error = (
                f"{host}:{port} is an address of the proxy's own namespace, not a server"
            )
            logging.info(f"netstub: refused {host}:{port}, an address of the namespace itself")


addons = [Transparent()]
