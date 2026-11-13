# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""mitmproxy addon for NET=mitm: live capture of the guest's HTTP(S), with no host treated specially.

The NET=mitm namespace (host/core/beanjar/netns.py) loads it into mitmweb, which runs in tun mode
(`--mode tun:mitm0`, the user's own mitmproxy 12.0.1 or newer: host/core/beanjar/mitmtool.py). Every
TCP and UDP packet of the guest is routed into mitmweb's TUN device, which reads each connection's
real destination from its packets, and mitmproxy forwards it there itself. There is no host list,
no stub response and no relay. This addon only adds generic behaviours:

- No upstream verification. The proxy is a debug tool: netns.py sets ssl_insecure, so a server's
  certificate is accepted whatever its root or names (LG's *.lgtvsdp.com is CN-only, from a
  private root) and its flows are decrypted.
- Best-effort TLS passthrough. A client or upstream TLS failure leaves that connection failed,
  then bypasses interception for subsequent connections to the SNI (else address) and port.
  Anchored, escaped ignore_hosts patterns preserve manual settings; at most 256 automatic
  entries are retained, oldest first. A bypass cannot repair unreachable servers or certificates
  the guest itself rejects. Each failure and new bypass is logged.
- A password for the inspector (WebPassword): mitmweb's web_password, from the file netns.py
  writes for the VM.
- Streaming. Bodies larger than NETSTUB_STREAM_LARGE bytes (default 1 MiB), and responses of
  unknown length that are not textual, are streamed rather than buffered; the flow stays in
  mitmweb without its body.
- A bounded history (FlowLimit): mitmweb keeps at most max_flows flows (vm.toml's
  network.mitm_flows, default 5000, 0 for no limit) and, if max_flow_bytes is set (vm.toml's
  network.mitm_flow_bytes, off by default), at most that many bytes of bodies, and drops the oldest
  finished flows beyond either, from memory and from the web view. Without it mitmweb grew by about 200 MB an hour.
- Ready (Ready): the file netns.py waits for (NETNS_MITM_READY) once mitmweb's servers run, after
  mitmproxy's UDP idle timeout is raised (tun_udp_timeout).

Where it depends on mitmproxy's internals it checks the feature, not the version
(tun_udp_timeout).
"""

import collections
import logging
import os
import re
import sys
import time

from mitmproxy import ctx, http

sys.dont_write_bytecode = True  # no __pycache__ in the source tree

TEXTUAL = ("text/", "json", "xml", "javascript", "x-www-form-urlencoded", "html")


def stream_limit():
    return int(os.environ.get("NETSTUB_STREAM_LARGE", str(1 << 20)))


def log(*fields):
    path = os.environ.get("NETSTUB_LOG")
    if not path:
        return
    with open(path, "a") as f:
        f.write(time.strftime("%Y-%m-%dT%H:%M:%S ") + " ".join(str(x) for x in fields) + "\n")


def endpoint(context):
    """'host:port' of a connection: the SNI if the client sent one, else the destination address."""
    if not context.server.address:
        return None
    address, port = context.server.address[:2]
    return f"{context.client.sni or address}:{port}"


class Capture:
    # Bound both our bookkeeping and the regex list on long-running VMs.
    MAX_TLS_BYPASSES = 256

    def __init__(self):
        self._tls_bypasses = collections.deque()

    def requestheaders(self, flow: http.HTTPFlow) -> None:
        length = flow.request.headers.get("content-length", "")
        if length.isdigit() and int(length) > stream_limit():
            flow.request.stream = True

    def responseheaders(self, flow: http.HTTPFlow) -> None:
        if (
            flow.response is None
            or flow.request.method == "HEAD"
            or flow.response.status_code in (101, 204, 304)
        ):
            return
        length = flow.response.headers.get("content-length", "")
        kind = flow.response.headers.get("content-type", "").lower()
        if (
            (int(length) > stream_limit())
            if length.isdigit()
            else not any(t in kind for t in TEXTUAL)
        ):
            flow.response.stream = True
            flow.comment = "Streamed body, not recorded"

    def _tls_failed(self, context, why) -> None:
        key = endpoint(context)
        if not key:
            return
        logging.warning(f"netstub: {why} for {key}")
        log("tls-failed", key, why.replace(" ", "-"))
        # NextLayer (mitmproxy 12.2.3 and 12.3.1) matches unbracketed host:port,
        # including IPv6, case-insensitively against address, SNI and Host header.
        pattern = "^" + re.escape(key) + "$"
        patterns = list(ctx.options.ignore_hosts)
        if pattern in patterns:
            return
        # Forget entries removed manually, without restoring the user's removals.
        self._tls_bypasses = collections.deque(p for p in self._tls_bypasses if p in patterns)
        if len(self._tls_bypasses) >= self.MAX_TLS_BYPASSES:
            patterns.remove(self._tls_bypasses.popleft())
        patterns.append(pattern)
        ctx.options.update(ignore_hosts=patterns)
        self._tls_bypasses.append(pattern)
        logging.warning(f"netstub: subsequent connections to {key} bypass TLS interception")
        log("tls-passthrough", key, why.replace(" ", "-"))

    def tls_failed_client(self, data) -> None:
        self._tls_failed(data.context, "client rejected the interception certificate")

    def tls_failed_server(self, data) -> None:
        self._tls_failed(data.context, "upstream TLS failed")

    def tcp_message(self, flow) -> None:
        # Undecrypted hosts (automatic or manual ignore_hosts) can carry long downloads:
        # keep only the latest chunk for display.
        del flow.messages[:-1]


class WebPassword:
    """The inspector's password: NETSTUB_WEB_PASSWORD_FILE (host/core/beanjar/netns.py writes it,
    owner-only) becomes mitmweb's web_password, as an argon2 hash, so the password is never on the
    command line, in the environment or in mitmweb's own log. mitmweb then answers 403 to anything
    without it, a login page in a browser; ?token=PASSWORD or Authorization: Bearer PASSWORD logs in
    (the cookie it sets lasts as long as this mitmweb)."""

    def load(self, loader) -> None:
        path = os.environ.get("NETSTUB_WEB_PASSWORD_FILE")
        if not path:
            return
        with open(path) as f:
            password = f.read().strip()
        if not password:
            raise RuntimeError(f"mitm_addon: {path} holds no password")
        import argon2
        from mitmproxy import ctx

        ctx.options.update(web_password=argon2.PasswordHasher().hash(password))


def flow_bytes(flow):
    """The bodies a flow keeps: request and response content, and TCP, UDP or WebSocket messages."""
    n = 0
    for m in (getattr(flow, "request", None), getattr(flow, "response", None)):
        raw = getattr(m, "raw_content", None)
        if isinstance(raw, bytes):
            n += len(raw)
    websocket = getattr(flow, "websocket", None)
    for messages in (getattr(flow, "messages", None), websocket and websocket.messages):
        n += sum(len(x.content) for x in messages or ())
    return n


class FlowLimit:
    """Keep at most max_flows flows in mitmweb's view (0: all of them) and, when max_flow_bytes is
    set (0: no limit), at most that many bytes of their bodies (flow_bytes). Beyond either, the
    oldest flows that are no longer live are removed with the view's own view.flows.remove, which
    drops them from the web view as well; a live flow (a connection still open) is never removed,
    so no traffic is cut. Removal by hand (the web view's delete or clear) is fine: the next
    flows simply fill up to the limits again."""

    def __init__(self):
        self.sizes = collections.OrderedDict()  # flow id -> flow_bytes, oldest first, as first seen
        self.total = 0

    def load(self, loader) -> None:
        loader.add_option(
            "max_flows",
            int,
            5000,  # beanjar's vmsettings.MITM_FLOWS; netns.py always passes it
            "Flows mitmweb keeps; the oldest finished ones are dropped beyond it. 0: no limit.",
        )
        loader.add_option(
            "max_flow_bytes",
            int,
            0,
            "Bytes of bodies the kept flows may hold; the oldest finished flows are dropped "
            "beyond it. 0: no limit.",
        )

    def _add(self, flow, *_) -> None:
        size = flow_bytes(flow)
        self.total += size - self.sizes.get(flow.id, 0)
        self.sizes[flow.id] = size  # an update keeps its place
        self._trim()

    def _forget(self, fid):
        self.total -= self.sizes.pop(fid)

    def _trim(self) -> None:
        from mitmproxy import ctx

        count, budget = ctx.options.max_flows, ctx.options.max_flow_bytes
        view = ctx.master.addons.get("view")
        if len(self.sizes) > view.store_count() + 64 or (budget > 0 and self.total > budget):
            for fid in [i for i in self.sizes if view.get_by_id(i) is None]:
                self._forget(fid)  # removed by hand
        over = lambda: (
            (count > 0 and view.store_count() > count) or (budget > 0 and self.total > budget)
        )
        if not over():
            return
        for fid in list(self.sizes):
            flow = view.get_by_id(fid)
            if flow is None:
                self._forget(fid)
            elif not flow.live:  # one still open stays: tried again on a later flow
                self._forget(fid)
                view.remove([flow])
            if not over():
                break

    # every hook through which a flow enters the view (mitmproxy.addons.view.View), the ones that
    # end a flow, so that a flow kept while live goes once it is done, and the ones that add to it
    requestheaders = request = response = error = _add
    tcp_start = tcp_end = tcp_error = _add
    udp_start = udp_end = udp_error = _add
    dns_request = dns_response = dns_error = _add
    websocket_message = websocket_end = _add


# How long an idle UDP flow is kept (s). mitmproxy's Python side closes an idle UDP flow
# after mitmproxy.proxy.server.UDP_TIMEOUT (20 s in 12.3.1) while its TUN side keeps that 5-tuple,
# so a client that reuses one UDP socket loses every datagram after a pause of 20 s or more, until
# it has been silent long enough for the TUN side to let go. connman's DNS proxy is such a client:
# its upstream socket to 10.0.2.3:53 lives as long as the guest, so the guest's every lookup timed
# out. Measured on a guest (one socket, a query after each idle gap): with 20 s, the query after 30 s
# idle was lost while those after 0 s, 10 s and 65 s were answered; with 600 s, those after 30, 65
# and 130 s were all answered, and connman's lookups worked again. The same applies to any UDP port
# (an NTP query on one socket after 30 s and 65 s idle was answered with 600 s).
TUN_UDP_TIMEOUT = 600


def tun_udp_timeout(server=None):
    """Raise mitmproxy's UDP idle timeout to TUN_UDP_TIMEOUT (a feature check: the module
    attribute must exist and be a number); a warning naming the version and False if it cannot."""
    import mitmproxy.version

    if server is None:
        try:
            import mitmproxy.proxy.server as server
        except ImportError:
            server = None
    current = getattr(server, "UDP_TIMEOUT", None)
    if isinstance(current, bool) or not isinstance(current, (int, float)):
        logging.warning(
            f"netstub: mitmproxy {mitmproxy.version.VERSION} has no proxy.server.UDP_TIMEOUT; "
            "idle UDP flows (the guest's DNS) may stop working"
        )
        return False
    if current < TUN_UDP_TIMEOUT:
        server.UDP_TIMEOUT = TUN_UDP_TIMEOUT
    return True


class Ready:
    """The ready file netns.py waits for (NETNS_MITM_READY), written once mitmweb's servers run,
    after the UDP timeout is raised when it serves a TUN device (tun_udp_timeout): netns.py routes
    nothing into the device before that file exists."""

    def running(self) -> None:
        from mitmproxy import ctx

        modes = [m for m in ctx.options.mode if m.startswith("tun")]
        if modes:
            tun_udp_timeout()
        ready = os.environ.get("NETNS_MITM_READY")
        if ready:
            with open(ready, "w") as f:
                f.write(f"{len(modes)}\n")


addons = [Capture(), WebPassword(), FlowLimit(), Ready()]
