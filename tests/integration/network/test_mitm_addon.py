#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for host/helpers/network/mitm_addon.py (mode=mitm). No network requests. They need
mitmproxy, the user's own (host/core/beanjar/mitmtool.py): Meson runs them with the Python of the
mitmweb it finds (tests/meson.build); under another Python they are skipped."""

import os
import re
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "host/helpers/network"))
import pytest

pytest.importorskip(
    "mitmproxy", reason="no mitmproxy: run with mitmweb's Python (pipx install mitmproxy)"
)
from mitmproxy import connection, http  # noqa: E402
from mitmproxy.addons.next_layer import NextLayer
from mitmproxy.proxy import context
from mitmproxy.proxy.layers import tls
from mitmproxy.test import taddons
import mitm_addon


def flow(request_headers=(), response_headers=None, method="GET", status=200):
    # make() fills in Content-Length from the (empty) body; the headers are set afterwards.
    f = SimpleNamespace(
        request=http.Request.make(method, "https://example.org/x"), response=None, comment=""
    )
    f.request.headers.pop("content-length", None)
    f.request.headers.update(dict(request_headers))
    if response_headers is not None:
        f.response = http.Response.make(status)
        f.response.headers.pop("content-length", None)
        f.response.headers.update(dict(response_headers))
    return f


def tls_context(sni, address=("93.184.215.14", 443)):
    return SimpleNamespace(client=SimpleNamespace(sni=sni), server=SimpleNamespace(address=address))


class Streaming(unittest.TestCase):
    def test_large_request_streams(self):
        f = flow({"content-length": str(2 << 20)})
        mitm_addon.Capture().requestheaders(f)
        self.assertTrue(f.request.stream)
        f = flow({"content-length": "100"})
        mitm_addon.Capture().requestheaders(f)
        self.assertFalse(f.request.stream)

    def test_responses(self):
        cases = [
            (
                {"content-length": str(2 << 20), "content-type": "application/json"},
                "GET",
                200,
                True,
            ),
            ({"content-type": "video/mp4"}, "GET", 200, True),  # open-ended binary
            ({"content-type": "application/json; charset=utf-8"}, "GET", 200, False),
            ({"content-length": "10", "content-type": "image/png"}, "GET", 200, False),
            ({"content-type": "video/mp4"}, "HEAD", 200, False),
            ({"content-type": "application/octet-stream"}, "GET", 204, False),
        ]
        for headers, method, status, streamed in cases:
            f = flow(response_headers=headers, method=method, status=status)
            mitm_addon.Capture().responseheaders(f)
            self.assertEqual(bool(f.response.stream), streamed, (headers, method, status))


class TlsFailure(unittest.TestCase):
    def run_hook(self, hook, contexts, manual=()):
        addon = mitm_addon.Capture()
        with (
            taddons.context(addon) as tctx,
            tempfile.TemporaryDirectory() as d,
            patch.dict(os.environ, {"NETSTUB_LOG": d + "/log"}),
        ):
            tctx.options.update(ignore_hosts=list(manual))
            for c in contexts:
                getattr(addon, hook)(SimpleNamespace(context=c))
            # mitmproxy 12's master logs through its event loop; done() takes its handler off the
            # root logger before taddons closes the loop (the next test would log into it).
            tctx.master.event_loop.run_until_complete(tctx.master.done())
            log = Path(d, "log").read_text() if Path(d, "log").exists() else ""
            return list(tctx.options.ignore_hosts), log

    def test_client_rejection_bypasses_subsequent_connections_once(self):
        hosts, log = self.run_hook(
            "tls_failed_client",
            [tls_context("Pinned.Example.org"), tls_context("Pinned.Example.org")],
        )
        self.assertEqual(hosts, [r"^Pinned\.Example\.org:443$"])
        self.assertEqual(log.count("tls-failed Pinned.Example.org:443 client-rejected"), 2)

    def test_upstream_failure_is_logged_by_address_without_sni(self):
        hosts, log = self.run_hook("tls_failed_server", [tls_context(None, ("203.0.113.9", 8443))])
        self.assertEqual(hosts, [r"^203\.0\.113\.9:8443$"])
        self.assertIn("tls-failed 203.0.113.9:8443 upstream-TLS-failed", log)

    def test_no_destination_is_ignored(self):
        hosts, log = self.run_hook("tls_failed_client", [tls_context("x.example", None)])
        self.assertEqual((hosts, log), ([], ""))

    def test_patterns_preserve_manual_settings_and_match_only_endpoint(self):
        hosts, log = self.run_hook(
            "tls_failed_server", [tls_context("a+b.example")], manual=[r"manual\.example"]
        )
        self.assertEqual(hosts[0], r"manual\.example")
        pattern = hosts[1]
        self.assertTrue(re.search(pattern, "A+B.EXAMPLE:443", re.IGNORECASE))
        for other in ("aaab.example:443", "a+b.example:8443", "x.a+b.example:443"):
            self.assertIsNone(re.search(pattern, other, re.IGNORECASE))
        self.assertEqual(log.count("tls-passthrough"), 1)

    def test_ipv6_address_is_escaped_in_mitmproxy_format(self):
        hosts, _ = self.run_hook("tls_failed_server", [tls_context(None, ("2001:db8::1", 443))])
        self.assertIsNotNone(re.search(hosts[0], "2001:db8::1:443"))
        self.assertIsNone(re.search(hosts[0], "2001:db8::2:443"))

    def test_next_layer_honors_fallback_and_manual_patterns(self):
        addon, next_layer = mitm_addon.Capture(), NextLayer()
        with taddons.context(addon, next_layer) as tctx:
            tctx.options.update(ignore_hosts=[r"^manual\.example:443$"])
            addon.tls_failed_client(SimpleNamespace(context=tls_context("pin.example")))
            client = connection.Client(peername=("127.0.0.1", 1000), sockname=("127.0.0.1", 443))
            c = context.Context(client, tctx.options)
            with (
                patch.object(next_layer, "_get_client_hello", return_value=None),
                patch.object(next_layer, "_get_host_header", return_value=None),
            ):
                for host, port, expected in (
                    ("pin.example", 443, True),
                    ("manual.example", 443, True),
                    ("pin.example", 8443, False),
                    ("other.example", 443, False),
                ):
                    c.server.address = ("203.0.113.9", port)
                    c.client.sni = host
                    self.assertEqual(next_layer._ignore_connection(c, b"", b""), expected)
            tctx.master.event_loop.run_until_complete(tctx.master.done())

    def test_existing_exact_manual_rule_is_not_owned_or_evicted(self):
        with patch.object(mitm_addon.Capture, "MAX_TLS_BYPASSES", 1):
            hosts, _ = self.run_hook(
                "tls_failed_server",
                [tls_context("manual.example"), tls_context("a.example"), tls_context("b.example")],
                manual=[r"^manual\.example:443$"],
            )
        self.assertEqual(hosts, [r"^manual\.example:443$", r"^b\.example:443$"])

    def test_automatic_entries_are_bounded_without_evicting_manual_patterns(self):
        with patch.object(mitm_addon.Capture, "MAX_TLS_BYPASSES", 2):
            hosts, _ = self.run_hook(
                "tls_failed_server",
                [tls_context(f"host{i}.example") for i in range(4)],
                manual=["manual"],
            )
        self.assertEqual(hosts, ["manual", r"^host2\.example:443$", r"^host3\.example:443$"])


class WebPassword(unittest.TestCase):
    """mitm_addon.WebPassword: the password file becomes mitmweb's web_password, hashed."""

    def load(self, content):
        from mitmproxy.tools.web.webaddons import WebAddon, WebAuth

        auth = WebAuth()
        with taddons.context(WebAddon(), auth) as tctx, tempfile.TemporaryDirectory() as d:
            env = {}
            if content is not None:
                Path(d, "pw").write_text(content)
                env["NETSTUB_WEB_PASSWORD_FILE"] = d + "/pw"
            try:
                with patch.dict(os.environ, env):
                    if "NETSTUB_WEB_PASSWORD_FILE" not in env:
                        os.environ.pop("NETSTUB_WEB_PASSWORD_FILE", None)
                    mitm_addon.WebPassword().load(None)
                return tctx.options.web_password, auth
            finally:
                tctx.master.event_loop.run_until_complete(tctx.master.done())

    def test_file_becomes_hashed_password(self):
        option, auth = self.load("s3cret-pw\n")
        self.assertTrue(option.startswith("$argon2"), option)
        self.assertNotIn("s3cret-pw", option)
        self.assertTrue(auth.is_valid_password("s3cret-pw"))
        self.assertFalse(auth.is_valid_password("s3cret"))
        self.assertFalse(auth.is_valid_password(""))

    def test_no_file_leaves_mitmweb_default(self):
        option, _ = self.load(None)
        self.assertEqual(option, "")

    def test_empty_file_refused(self):
        with self.assertRaisesRegex(RuntimeError, "no password"):
            self.load("\n")


class FlowLimit(unittest.TestCase):
    """mitm_addon.FlowLimit: the view keeps max_flows flows, the oldest finished ones go first, a
    live one stays, 0 keeps everything."""

    def run_flows(self, limit, flows, live=(), budget=0, body=0):
        from mitmproxy.addons import view
        from mitmproxy.test import tflow

        v, limiter = view.View(), mitm_addon.FlowLimit()
        with taddons.context(v, limiter) as tctx:
            try:
                tctx.configure(limiter, max_flows=limit, max_flow_bytes=budget)
                made = []
                for i in range(flows):
                    f = tflow.tflow(resp=True)
                    f.request.content, f.response.content = b"q" * body, b"r" * body
                    f.live = i in live
                    made.append(f)
                    v.requestheaders(f)  # the view first, as in mitmweb
                    limiter.requestheaders(f)
                return [f.id for f in made], [f.id for f in v]
            finally:
                tctx.master.event_loop.run_until_complete(tctx.master.done())

    def test_default_is_vmsettings(self):
        # the addon runs in mitmproxy's own Python, without beanjar: its default repeats this one
        sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "host/core"))
        from beanjar import vmsettings

        with taddons.context(mitm_addon.FlowLimit()) as tctx:
            try:
                self.assertEqual(tctx.options.max_flows, vmsettings.MITM_FLOWS)
            finally:
                tctx.master.event_loop.run_until_complete(tctx.master.done())

    def test_oldest_dropped(self):
        made, kept = self.run_flows(3, 10)
        self.assertEqual(kept, made[-3:])

    def test_live_flow_kept(self):
        made, kept = self.run_flows(3, 10, live={1})
        self.assertEqual(kept, [made[1]] + made[-2:])

    def test_byte_budget(self):
        # 10 flows of 2 x 100 bytes, 700 bytes allowed: the newest three stay
        made, kept = self.run_flows(1000, 10, budget=700, body=100)
        self.assertEqual(kept, made[-3:])

    def test_byte_budget_keeps_live(self):
        made, kept = self.run_flows(1000, 10, live={0}, budget=700, body=100)
        self.assertEqual(kept, [made[0]] + made[-2:])

    def test_byte_budget_off_by_default(self):
        with taddons.context(mitm_addon.FlowLimit()) as tctx:
            try:
                self.assertEqual(tctx.options.max_flow_bytes, 0)
            finally:
                tctx.master.event_loop.run_until_complete(tctx.master.done())

    def test_flow_bytes(self):
        from mitmproxy.test import tflow

        f = tflow.tflow(resp=True)
        f.request.content, f.response.content = b"a" * 10, b"b" * 32
        self.assertEqual(mitm_addon.flow_bytes(f), 42)
        t = tflow.ttcpflow()
        self.assertEqual(mitm_addon.flow_bytes(t), sum(len(m.content) for m in t.messages))

    def test_unlimited(self):
        made, kept = self.run_flows(0, 20)
        self.assertEqual(kept, made)


class Ready(unittest.TestCase):
    """The ready file netns.py waits for, and the UDP idle timeout of a TUN device."""

    def test_ready_file_after_the_udp_timeout(self):
        with tempfile.TemporaryDirectory() as d:
            ready = os.path.join(d, "mitmweb.ready")
            with patch.dict(os.environ, {"NETNS_MITM_READY": ready}):
                with taddons.context(mitm_addon.Ready()) as tctx:
                    tctx.options.update(mode=["regular@8080"])
                    with patch.object(mitm_addon, "tun_udp_timeout") as raised:
                        mitm_addon.Ready().running()
                    raised.assert_not_called()  # no TUN device: nothing to raise
                    self.assertTrue(os.path.exists(ready))
                    os.unlink(ready)
                    tctx.options.update(mode=["tun:mitm0"])
                    with patch.object(mitm_addon, "tun_udp_timeout") as raised:
                        mitm_addon.Ready().running()
                    raised.assert_called_once_with()
                    self.assertTrue(os.path.exists(ready))

    def test_udp_timeout_raised_by_feature(self):
        server = SimpleNamespace(UDP_TIMEOUT=20)
        self.assertTrue(mitm_addon.tun_udp_timeout(server))
        self.assertEqual(server.UDP_TIMEOUT, mitm_addon.TUN_UDP_TIMEOUT)
        server = SimpleNamespace(UDP_TIMEOUT=3600)
        self.assertTrue(mitm_addon.tun_udp_timeout(server))
        self.assertEqual(server.UDP_TIMEOUT, 3600)  # never lowered
        for missing in (SimpleNamespace(), SimpleNamespace(UDP_TIMEOUT="20")):
            with self.assertLogs(level="WARNING") as logs:
                self.assertFalse(mitm_addon.tun_udp_timeout(missing))
            self.assertIn("UDP_TIMEOUT", logs.output[0])

    def test_the_real_attribute(self):
        import mitmproxy.proxy.server as server

        self.assertIsInstance(server.UDP_TIMEOUT, (int, float))


if __name__ == "__main__":
    unittest.main()
