#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-web --hosts (host/web/beanjar_web/server.py): the list asks and shows only the allowed
hosts' VMs (local: this machine's, left out unless named); every route of the route table that
names a host refuses another with 403 before any `beanjar` runs; an unknown host is a start-up
error (exit 2) for serve and start alike; without --hosts nothing changes. The manager is stood in
by Server.run and Server.spawn here, and by a little script for `host list`."""

import contextlib
import io
import json
import os
import re
import sys
import unittest
from unittest import mock

import pytest

pytest.importorskip("aiohttp", reason="python3-aiohttp is not installed")
from aiohttp.test_utils import TestClient, TestServer  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../host/web"))
from beanjar_web import access, server  # noqa: E402

ROWS = {
    "local": [{"name": "tv", "host": None, "state": "Running"}],
    "pi": [{"name": "w16r", "host": "pi", "state": "Off"}],
    "rk-2": [{"name": "k8", "host": "rk-2", "state": "Running"}],
}
STATIC = os.path.dirname(os.path.abspath(__file__))  # any directory: no static file is asked
HOST_LIST = [{"name": "pi", "uri": "ssh://pi"}, {"name": "rk-2", "uri": "ssh://rk"}]


class FakeManager:
    """Server.run and Server.spawn: `list --json [--host H]` answered from ROWS, every call kept."""

    def __init__(self, failing=()):
        self.calls = []
        self.failing = set(failing)

    async def run(self, words, timeout=120):
        self.calls.append(list(words))
        if words[:2] == ["list", "--json"]:
            host = words[3] if words[2:3] == ["--host"] else None
            if host in self.failing:
                return 1, b"", json.dumps({"error": f"host {host}: unreachable"})
            rows = ROWS[host] if host else [r for h in ROWS.values() for r in h]
            # a stray row of another host: the server filters what it shows anyway
            rows = rows + ([{"name": "x", "host": "stray"}] if host else [])
            return 0, json.dumps(rows).encode(), ""
        return 1, b"", "beanjar: unexpected"

    async def spawn(self, words, stdin=False):
        self.calls.append(list(words))
        raise OSError("no manager here")


def make(hosts=None, failing=()):
    s = server.Server(["beanjar"], access.Access("none"), STATIC, STATIC,
                      hosts=hosts)  # fmt: skip
    fake = FakeManager(failing)
    s.run, s.spawn = fake.run, fake.spawn
    return s, fake


def route_path(canonical, host):
    """A concrete path of a route, host in its {host}."""
    values = {"host": host, "name": "tv", "action": "start"}
    return re.sub(r"\{(\w+)(?::[^}]*)?\}", lambda m: values.get(m[1], "x"), canonical)


class Web(unittest.IsolatedAsyncioTestCase):
    async def client(self, s):
        c = TestClient(TestServer(s.app()))
        await c.start_server()
        self.addAsyncCleanup(c.close)
        return c

    async def names(self, c):
        r = await c.get("/api/vms")
        self.assertEqual(r.status, 200)
        return sorted(f"{v['host'] or 'local'}/{v['name']}" for v in await r.json())

    async def test_list_one_host(self):
        s, fake = make(("rk-2",))
        c = await self.client(s)
        self.assertEqual(await self.names(c), ["rk-2/k8"])
        self.assertEqual(fake.calls, [["list", "--json", "--host", "rk-2"]])

    async def test_list_local_and_host(self):
        s, fake = make(("local", "pi"))
        c = await self.client(s)
        self.assertEqual(await self.names(c), ["local/tv", "pi/w16r"])
        self.assertEqual(
            sorted(fake.calls), [["list", "--json", "--host", h] for h in ("local", "pi")]
        )

    async def test_list_failures(self):
        s, _ = make(("local", "pi"), failing=("pi",))
        c = await self.client(s)
        self.assertEqual(await self.names(c), ["local/tv"])  # what answered
        s, _ = make(("pi",), failing=("pi",))
        c = await self.client(s)
        r = await c.get("/api/vms")
        self.assertEqual(r.status, 502)
        self.assertEqual((await r.json())["error"], "host pi: unreachable")

    async def test_unchanged_without_hosts(self):
        s, fake = make()
        c = await self.client(s)
        self.assertEqual(await self.names(c), ["local/tv", "pi/w16r", "rk-2/k8"])
        self.assertEqual(fake.calls, [["list", "--json"]])
        for canonical in self.host_routes(s):
            method, path = canonical
            r = await c.request(method, route_path(path, "local"))
            self.assertNotEqual(r.status, 403, path)

    def host_routes(self, s, app=None):
        """(method, canonical path) of every route naming a host, from the route table (the main
        app's, or app's)."""
        found = []
        for route in (app or s.app()).router.routes():
            canonical = route.resource.canonical
            if canonical.startswith("/vm/"):
                self.assertIn("{host}", canonical, "a /vm/ route without {host}")
            if "{host}" in canonical:
                # a route of any method (the inspector's): asked as GET and as PUT
                methods = ("GET", "PUT") if route.method == "*" else (route.method,)
                found += [(m, canonical) for m in methods]
        return found

    async def test_every_host_route_refused(self):
        s, fake = make(("rk-2",))
        c = await self.client(s)
        routes = self.host_routes(s)
        kinds = {p.removeprefix("/vm/{host}/{name}") for _, p in routes}
        for kind in ("/", "/{action}", "/operation", "/vnc", "/layout.json",
                     "/screenshot.png", "/rfb", "/remote", "/audio", "/video",
                     "/inspector.json"):  # fmt: skip
            self.assertIn(kind, kinds)
        for method, path in routes:
            for host in ("local", "pi", "unknown"):
                url = route_path(path, host)
                for query in ("", "?thumbnail=1&page=abcdefgh&format=pcm&bitrate=800"):
                    r = await c.request(method, url + query)
                    self.assertEqual(r.status, 403, f"{method} {url}")
                    if method != "HEAD":
                        self.assertIn("not served here", await r.text())
        # the WebSockets too, refused in the handshake
        for kind in ("rfb", "remote", "audio", "video"):
            with self.assertRaises(Exception) as e:
                await c.ws_connect(f"/vm/local/tv/{kind}")
            self.assertEqual(getattr(e.exception, "status", None), 403, kind)
        self.assertEqual(fake.calls, [], "a beanjar command ran for a refused host")
        # the allowed host passes the check (here to the stand-in manager's failure)
        r = await c.get("/vm/rk-2/k8/layout.json")
        self.assertEqual(r.status, 502)
        self.assertEqual(fake.calls, [["remote", "layout", "rk-2/k8", "--json"]])


class InspectorHosts(unittest.IsolatedAsyncioTestCase):
    async def test_every_inspector_route_refused(self):
        """The inspector origin's routes (Server.inspector_app) for another host than --hosts:
        refused even with that VM's grant, no beanjar command run."""
        s, fake = make(("rk-2",))
        app = s.inspector_app()
        routes = Web.host_routes(self, s, app)
        kinds = {p.removeprefix("/vm/{host}/{name}") for _, p in routes}
        self.assertEqual(kinds, {"/mitm", "/mitm/{path}"})
        c = TestClient(TestServer(app))
        await c.start_server()
        self.addAsyncCleanup(c.close)
        for method, path in routes:
            for host in ("local", "pi", "unknown"):
                url = route_path(path, host)
                r = await c.request(method, f"{url}?grant={s.inspectors.grant(host + '/tv')}")
                self.assertEqual(r.status, 403, f"{method} {url}")
                if method != "HEAD":
                    self.assertIn("not served here", await r.text())
        self.assertEqual(fake.calls, [], "a beanjar command ran for a refused host")


class TestStartUp:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path):
        self.tmp = str(tmp_path)
        os.makedirs(os.path.join(self.tmp, "novnc", "core"))
        open(os.path.join(self.tmp, "novnc", "core", "rfb.js"), "w").close()
        open(os.path.join(self.tmp, "house.svg"), "w").close()
        self.log = os.path.join(self.tmp, "calls.log")
        script = os.path.join(self.tmp, "beanjar.py")
        with open(script, "w") as f:
            f.write(
                "import json, sys\n"
                f"open({self.log!r}, 'a').write(' '.join(sys.argv[1:]) + '\\n')\n"
                "assert sys.argv[1:] == ['host', 'list', '--json'], sys.argv\n"
                f"print(json.dumps({HOST_LIST!r}))\n"
            )
        self.beanjar = f"{sys.executable} {script}"

    def args(self, *hosts):
        words = ["--auth", "none", "--novnc", os.path.join(self.tmp, "novnc"),
                 "--icons", self.tmp, "--beanjar", self.beanjar]  # fmt: skip
        for h in hosts:
            words += ["--hosts", h]
        return words

    def prepare(self, *hosts):
        p = server.parser()
        a = p.parse_args(self.args(*hosts))
        server.prepare(p, a)
        return a.hosts

    def refused(self, argv):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), pytest.raises(SystemExit) as e:
            server.main(argv)
        assert e.value.code == 2
        return err.getvalue()

    def test_known(self):
        assert self.prepare("rk-2") == ("rk-2",)
        assert self.prepare("local, pi,local") == ("local", "pi")
        assert self.prepare() is None

    def test_local_alone_asks_nothing(self):
        assert self.prepare("local") == ("local",)
        assert not os.path.exists(self.log)

    def test_unknown_is_an_error(self):
        for command in ("serve", "start", "enable"):
            with mock.patch("beanjar_web.unit.start") as start, \
                    mock.patch("beanjar_web.unit.enable") as enable:  # fmt: skip
                err = self.refused([command, *self.args("rk-3")])
            start.assert_not_called()
            enable.assert_not_called()
            assert "--hosts: no host named rk-3" in err
            assert "pi, rk-2" in err

    def test_bad_forms(self):
        assert "--hosts once" in self.refused(["serve", *self.args("pi", "local")])
        assert "empty host name" in self.refused(["serve", *self.args("pi,")])
        assert "not a host name" in self.refused(["serve", *self.args("a/b")])

    def test_manager_fails(self):
        p = server.parser()
        a = p.parse_args(self.args("pi"))
        a.beanjar = f"{sys.executable} -c 'import sys; sys.exit(\"beanjar: broken config\")'"
        err = io.StringIO()
        with contextlib.redirect_stderr(err), pytest.raises(SystemExit):
            server.prepare(p, a)
        assert "cannot read the configured hosts (beanjar host list): broken config" in err.getvalue()  # fmt: skip
