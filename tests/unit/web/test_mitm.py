#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-web's traffic inspector (host/web/beanjar_web/mitm.py): /vm/HOST/NAME/mitm/ of its own
origin (Server.inspector_app, behind a per-VM grant) against a stand-in mitmweb (its login as the pinned 12.2.3's: a Bearer password or its signed
session cookie, else 403; its XSRF cookie and check; /updates) and a stand-in manager whose `mitm
connect` is a real process that prints where the inspector is and waits for its stdin to end.

Checked: the inspector is not on the main origin, whose cookie it never takes; its grant per VM
from the main origin's inspector.json; the main origin refuses the inspector page's fetches, POSTs
and WebSockets; the server's own auth first; --hosts; a VM without mitm and a stopped one (the manager's
refusal, 409); HTTP and WebSocket relayed both ways; the login done here (the password and the
session cookie never in the browser, the browser's cookies never upstream but mitmweb's XSRF one,
whose path becomes the prefix); File > Save's /flows/dump made relative; a new password after a
VM restart (a new `mitm connect`, the request tried again); one `mitm connect` shared, ended when
idle, at shutdown, and not kept after a failure."""

import asyncio
import json
import os
import sys
import tempfile
import unittest

import pytest

pytest.importorskip("aiohttp", reason="python3-aiohttp is not installed")
from aiohttp import WSMsgType, web  # noqa: E402
from aiohttp.test_utils import TestClient, TestServer  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../host/web"))
from beanjar_web import access, mitm, server  # noqa: E402

STATIC = os.path.dirname(os.path.abspath(__file__))
SCRIPT = (
    b'a=()=>location.replace("/flows/dump?filter="+i);function hu(){return location.replace('
    b'"/flows/dump")}fetch("./flows")'
)

NO_MITMWEB = "mitmweb not found. Install mitmproxy 12.0.1 or newer: pipx install mitmproxy"

# the stand-in manager: `mitm connect --json SPEC` and `mitm show --json SPEC`, from state.json
FAKE_BEANJAR = r"""
import json, os, sys
d = os.environ["FAKE_DIR"]
words = sys.argv[1:]
with open(os.path.join(d, "calls.log"), "a") as f:
    f.write(" ".join(words) + "\n")
state = json.load(open(os.path.join(d, "state.json")))
vm = state.get(words[-1])
if words[:2] == ["mitm", "show"]:
    if vm is None:
        sys.stderr.write(json.dumps({"error": "no VM named x"}) + "\n")
        sys.exit(1)
    print(json.dumps({"name": "tv", "mode": vm["mode"], "status": vm["status"],
                      "ready": vm["status"] == "Running", "url": vm["url"], "local_url": None,
                      "password_file": "/secret/path", "mitmweb": vm.get("mitmweb")}))
    sys.exit(0)
assert words[:3] == ["mitm", "connect", "--json"], words
if vm is None or vm["mode"] != "mitm" or vm["status"] != "Running":
    why = "VM tv is not running" if vm and vm["mode"] == "mitm" else "no traffic inspector"
    sys.stderr.write(json.dumps({"error": why}) + "\n")
    sys.exit(1)
print(json.dumps({"url": vm["url"], "password": vm["password"]}), flush=True)
pid = os.path.join(d, f"connect-{os.getpid()}")
open(pid, "w").close()
sys.stdin.buffer.read()
os.unlink(pid)
"""


class FakeMitmweb:
    """mitmweb 12.2.3's login and XSRF rules, enough of them: every request needs the signed
    session cookie (mitmproxy-auth-PORT) or the password (Bearer), the latter answered with the
    cookie; non-GET requests need X-XSRFToken equal to the _mitmproxy_xsrf cookie; /updates sets
    that cookie in its handshake and echoes."""

    def __init__(self):
        self.password = "pw1"
        self.session = "s1"  # a new mitmweb: a new cookie secret
        self.bearer_checks = 0
        self.seen = []  # the headers of each request

    def app(self):
        app = web.Application(middlewares=[self.login])
        app.router.add_get("/", self.index)
        app.router.add_get("/static/index-AbC_1.js", self.script)
        app.router.add_get("/static/vendor-x.js", self.vendor)
        app.router.add_get("/flows", self.flows)
        app.router.add_put("/flows/{id}", self.update)
        app.router.add_get("/updates", self.updates)
        return app

    @web.middleware
    async def login(self, request, handler):
        self.seen.append(dict(request.headers))
        cookie = f"mitmproxy-auth-18083={self.session}"
        if request.method not in ("GET", "HEAD"):
            token = request.headers.get("X-XSRFToken")
            if not token or token != request.cookies.get("_mitmproxy_xsrf"):
                raise web.HTTPForbidden(text="XSRF cookie does not match")
        if request.cookies.get("mitmproxy-auth-18083") == self.session:
            return await handler(request)
        auth = request.headers.get("Authorization", "")
        self.bearer_checks += 1
        if auth != f"Bearer {self.password}":
            raise web.HTTPForbidden(text="login")
        response = await handler(request)
        response.headers.add("Set-Cookie", f"{cookie}; HttpOnly; Path=/; SameSite=Strict")
        return response

    async def index(self, request):
        return web.Response(text='<script src="./static/index-AbC_1.js"></script>',
                            content_type="text/html", headers={"Server": "mitmproxy 12.2.3"})  # fmt: skip

    async def script(self, request):
        return web.Response(body=SCRIPT, content_type="text/javascript")

    async def vendor(self, request):
        return web.Response(body=SCRIPT, content_type="text/javascript")

    async def flows(self, request):
        return web.json_response({"query": request.rel_url.raw_query_string})

    async def update(self, request):
        return web.json_response({"id": request.match_info["id"], "body": await request.text()})

    async def updates(self, request):
        ws = web.WebSocketResponse()
        ws.headers.add("Set-Cookie", "_mitmproxy_xsrf=2|ab|cd|1; Path=/; SameSite=Strict")
        await ws.prepare(request)
        await ws.send_str('{"type": "hello"}')
        async for msg in ws:
            if msg.type == WSMsgType.TEXT:
                await ws.send_str("echo " + msg.data)
        return ws


class Test(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = tmp.name
        script = os.path.join(self.dir, "beanjar.py")
        with open(script, "w") as f:
            f.write(FAKE_BEANJAR)
        os.environ["FAKE_DIR"] = self.dir
        self.mitmweb = FakeMitmweb()
        self.upstream = TestServer(self.mitmweb.app())
        await self.upstream.start_server()
        self.addAsyncCleanup(self.upstream.close)
        url = f"http://127.0.0.1:{self.upstream.port}/"
        self.state = {
            "local/tv": {"mode": "mitm", "status": "Running", "url": url, "password": "pw1"},
            "local/direct": {"mode": "direct", "status": "Not running", "url": None},
            "local/off": {"mode": "mitm", "status": "Not running", "url": None},
            "local/nomitm": {
                "mode": "mitm",
                "status": f"Inspector off: {NO_MITMWEB}",
                "url": None,
                "mitmweb": {"version": None, "path": None, "problem": NO_MITMWEB},
            },
        }
        self.save()
        self.beanjar = [sys.executable, script]

    def save(self):
        with open(os.path.join(self.dir, "state.json"), "w") as f:
            json.dump(self.state, f)

    def calls(self):
        try:
            with open(os.path.join(self.dir, "calls.log")) as f:
                return f.read().splitlines()
        except FileNotFoundError:
            return []

    def connects(self):
        """The `mitm connect`s running now."""
        return [n for n in os.listdir(self.dir) if n.startswith("connect-")]

    async def main_client(self, acc=None, hosts=None, idle=mitm.IDLE):
        """The Server and a client of its main origin."""
        s = server.Server(self.beanjar, acc or access.Access("none"), STATIC, STATIC,
                          hosts=hosts)  # fmt: skip
        s.inspectors.idle = idle
        m = TestClient(TestServer(s.app()))
        await m.start_server()
        self.addAsyncCleanup(m.close)
        return s, m

    async def client(self, acc=None, hosts=None, idle=mitm.IDLE, grants=("local/tv",)):
        """The Server and a client of its inspector origin, holding the grants of the VMs named
        (as a page of the main origin's link opened), or the Server, the inspector client and the
        main client with acc."""
        s, m = await self.main_client(acc, hosts, idle)
        c = TestClient(TestServer(s.inspector_app()))
        await c.start_server()
        self.addAsyncCleanup(c.close)
        s.inspectors.port = c.port
        for spec in grants:
            r = await c.get(f"/vm/{spec}/mitm/?grant={s.inspectors.grant(spec)}",
                            allow_redirects=False)  # fmt: skip
            self.assertEqual(r.status, 302, await r.text())
        self.main = m
        return s, c

    async def wait_for(self, condition, timeout=5.0):
        for _ in range(int(timeout / 0.05)):
            if condition():
                return
            await asyncio.sleep(0.05)
        self.fail("timed out")

    async def test_not_on_the_main_origin(self):
        """The inspector is no route of the main origin, whatever its session: a script in
        mitmweb's page (the guest's traffic) must not run where it could drive the VMs."""
        acc = access.Access("token", "t0ken")
        _s, m = await self.main_client(acc)
        self.assertEqual((await m.get("/?token=t0ken", allow_redirects=False)).status, 302)
        for path in ("/vm/local/tv/mitm/", "/vm/local/tv/mitm", "/vm/local/tv/mitm/flows"):
            self.assertEqual((await m.get(path, allow_redirects=False)).status, 404, path)
        self.assertEqual([x for x in self.calls() if "connect" in x], [])

    async def test_main_origin_refuses_the_inspector_page(self):
        """What the inspector origin's page (another port: the same site) could send the main
        origin with its cookie: a fetch, a POST, a WebSocket, refused; opening a page is not."""
        acc = access.Access("token", "t0ken")
        _s, m = await self.main_client(acc)
        self.assertEqual((await m.get("/?token=t0ken", allow_redirects=False)).status, 302)
        theirs = f"http://127.0.0.1:{m.port + 1}"  # the inspector's
        same_site = {"Sec-Fetch-Site": "same-site", "Sec-Fetch-Mode": "cors",
                     "Sec-Fetch-Dest": "empty"}  # fmt: skip
        self.assertEqual((await m.get("/api/session")).status, 200)
        r = await m.get("/api/session", headers=same_site)
        self.assertEqual(r.status, 403)
        r = await m.get("/vm/local/tv/inspector.json", headers=same_site)
        self.assertEqual(r.status, 403)
        r = await m.get("/vm/local/tv/inspector.json", headers={**same_site, "Sec-Fetch-Dest": "iframe",
                        "Sec-Fetch-Mode": "navigate"})  # fmt: skip
        self.assertEqual(r.status, 403, "framed")
        for headers in ({"Origin": theirs}, {"Sec-Fetch-Site": "same-site"}):
            r = await m.post("/vm/local/tv/stop", headers=headers)
            self.assertEqual(r.status, 403, headers)
            with self.assertRaises(Exception) as e:
                await m.ws_connect("/vm/local/tv/remote", headers=headers)
            self.assertEqual(getattr(e.exception, "status", None), 403, headers)
        # a page opened from there (a top-level navigation) is the user's
        r = await m.get("/api/session", headers={**same_site, "Sec-Fetch-Mode": "navigate",
                        "Sec-Fetch-Dest": "document"})  # fmt: skip
        self.assertEqual(r.status, 200)
        self.assertEqual([x for x in self.calls() if "stop" in x], [])

    async def test_inspector_origin_takes_no_main_cookie(self):
        """The inspector origin serves only the inspectors, never takes beanjar-web's session
        cookie (cookies are not kept apart by port) and never passes it or its own grant to
        mitmweb; another origin's fetches and writes are refused there too."""
        acc = access.Access("token", "t0ken")
        s, c = await self.client(acc, grants=())
        c.session.cookie_jar.update_cookies({"beanjar_web": acc.session})
        for path in ("/vm/local/tv/mitm/", "/vm/local/tv/mitm/flows"):
            self.assertEqual((await c.get(path)).status, 403, path)
        for path in ("/", "/api/vms", "/api/session", "/vm/local/tv/", "/vm/local/tv/inspector.json",
                     "/index.html"):  # fmt: skip
            self.assertEqual((await c.get(path)).status, 404, path)
        r = await c.get("/vm/local/tv/mitm/?grant=" + s.inspectors.grant("local/tv"))
        self.assertEqual(r.status, 200)
        r = await c.get("/vm/local/tv/mitm/flows")
        self.assertEqual(r.status, 200)
        for seen in self.mitmweb.seen:
            self.assertNotIn("beanjar_web", seen.get("Cookie", ""))
            self.assertNotIn(mitm.GRANT_COOKIE, seen.get("Cookie", ""))
        r = await c.get("/vm/local/tv/mitm/flows", headers={"Sec-Fetch-Site": "same-site",
                        "Sec-Fetch-Mode": "cors", "Sec-Fetch-Dest": "empty"})  # fmt: skip
        self.assertEqual(r.status, 403)
        r = await c.put("/vm/local/tv/mitm/flows/abc", data="x",
                        headers={"Origin": f"http://127.0.0.1:{self.main.port}"})  # fmt: skip
        self.assertEqual(r.status, 403)

    async def test_proxy_and_login(self):
        _s, c = await self.client()
        r = await c.get("/vm/local/tv/mitm", allow_redirects=False)
        self.assertEqual((r.status, r.headers["Location"]), (302, "/vm/local/tv/mitm/"))
        c.session.cookie_jar.update_cookies({"beanjar_web": "secret"})
        r = await c.get("/vm/local/tv/mitm/")
        self.assertEqual(r.status, 200)
        self.assertIn("./static/index-AbC_1.js", await r.text())
        self.assertNotIn("Set-Cookie", r.headers, "mitmweb's session stays here")
        self.assertEqual(r.headers["Server"], "mitmproxy 12.2.3")
        r = await c.get("/vm/local/tv/mitm/flows?filter=~d%20x&a=1")
        self.assertEqual(await r.json(), {"query": "filter=~d%20x&a=1"})
        self.assertEqual(self.mitmweb.bearer_checks, 1, "the password checked once")
        for seen in self.mitmweb.seen:
            self.assertEqual(seen.get("Authorization"), "Bearer pw1")
            self.assertNotIn("beanjar_web", seen.get("Cookie", ""))
            self.assertNotIn("Origin", seen)
        self.assertEqual(self.mitmweb.seen[-1]["Cookie"], "mitmproxy-auth-18083=s1")
        # the page's script: File > Save relative, nothing else changed; others as they are
        r = await c.get("/vm/local/tv/mitm/static/index-AbC_1.js")
        body = await r.read()
        self.assertEqual(body, SCRIPT.replace(b'"/flows/dump', b'"./flows/dump'))
        self.assertEqual(body.count(b'location.replace("./flows/dump'), 2)
        self.assertEqual(int(r.headers["Content-Length"]), len(body))
        self.assertEqual(await (await c.get("/vm/local/tv/mitm/static/vendor-x.js")).read(), SCRIPT)
        self.assertEqual(len(self.connects()), 1, "one mitm connect for them all")
        self.assertEqual(
            [x for x in self.calls() if "connect" in x], ["mitm connect --json local/tv"]
        )

    async def test_websocket_and_xsrf(self):
        _s, c = await self.client()
        ws = await c.ws_connect("/vm/local/tv/mitm/updates")
        self.assertEqual((await ws.receive()).data, '{"type": "hello"}')
        await ws.send_str("filter")
        self.assertEqual((await ws.receive()).data, "echo filter")
        # the XSRF cookie mitmweb set in its handshake, under the prefix: the page sends it back
        (cookie,) = [m for m in c.session.cookie_jar if m.key == "_mitmproxy_xsrf"]
        self.assertEqual(cookie["path"], "/vm/local/tv/mitm/")
        self.assertEqual(cookie.value, "2|ab|cd|1")
        r = await c.put("/vm/local/tv/mitm/flows/abc", data="x",
                        headers={"X-XSRFToken": "2|ab|cd|1"})  # fmt: skip
        self.assertEqual(await r.json(), {"id": "abc", "body": "x"})
        r = await c.put("/vm/local/tv/mitm/flows/abc", data="x")
        self.assertEqual(r.status, 403, "mitmweb's own XSRF check still applies")
        # a cross-origin PUT never reaches it
        r = await c.put("/vm/local/tv/mitm/flows/abc", data="x",
                        headers={"Origin": "http://evil.example", "X-XSRFToken": "2|ab|cd|1"})  # fmt: skip
        self.assertEqual((r.status, await r.text()), (403, "cross-origin request"))
        with self.assertRaises(Exception) as e:
            await c.ws_connect("/vm/local/tv/mitm/updates", origin="http://evil.example")
        self.assertEqual(getattr(e.exception, "status", None), 403)
        await ws.close()

    async def test_restarted_vm(self):
        """A new password and a new mitmweb (its cookies void): the GET is refused, a new `mitm
        connect` reads the new password, and the request is answered."""
        _s, c = await self.client()
        self.assertEqual((await c.get("/vm/local/tv/mitm/flows")).status, 200)
        self.mitmweb.password, self.mitmweb.session = "pw2", "s2"
        self.state["local/tv"]["password"] = "pw2"
        self.save()
        r = await c.get("/vm/local/tv/mitm/flows")
        self.assertEqual(r.status, 200)
        self.assertEqual(len([x for x in self.calls() if x.endswith("connect --json local/tv")]), 2)
        await self.wait_for(lambda: len(self.connects()) == 1)  # the old one ended
        ws = await c.ws_connect("/vm/local/tv/mitm/updates")
        self.assertEqual((await ws.receive()).data, '{"type": "hello"}')
        await ws.close()
        # a password that is wrong for good: the second refusal is the answer
        self.mitmweb.password, self.mitmweb.session = "pw3", "s3"
        self.assertEqual((await c.get("/vm/local/tv/mitm/flows")).status, 403)

    async def test_refusals(self):
        _s, c = await self.client(grants=("local/tv", "local/direct", "local/off"))
        for vm, message in (("direct", "no traffic inspector"), ("off", "VM tv is not running")):
            r = await c.get(f"/vm/local/{vm}/mitm/")
            self.assertEqual(r.status, 409)
            self.assertEqual(await r.text(), f"beanjar-web: the traffic inspector: {message}")
        self.assertEqual(self.connects(), [])
        self.assertEqual(_s.inspectors.upstreams, {}, "a failure is not kept")
        # the state the viewer asks for, without the URL or the password file
        r = await self.main.get("/vm/local/tv/inspector.json")
        self.assertEqual(
            await r.json(),
            {
                "mode": "mitm",
                "status": "Running",
                "ready": True,
                "problem": None,
                "url": f"http://127.0.0.1:{c.port}/vm/local/tv/mitm/?grant="
                + _s.inspectors.grant("local/tv"),
            },
        )
        r = await self.main.get("/vm/local/direct/inspector.json")
        self.assertEqual(
            await r.json(),
            {"mode": "direct", "status": "Not running", "ready": False, "problem": None,
             "url": None},
        )  # fmt: skip
        # started without a usable mitmweb: why, with what to install, for the viewer to show
        r = await self.main.get("/vm/local/nomitm/inspector.json")
        self.assertEqual(
            await r.json(),
            {
                "mode": "mitm",
                "status": f"Inspector off: {NO_MITMWEB}",
                "ready": False,
                "problem": NO_MITMWEB,
                "url": None,
            },
        )
        r = await self.main.get("/vm/local/nope/inspector.json")
        self.assertEqual((r.status, await r.json()), (502, {"error": "no VM named x"}))
        # an inspector that does not answer at all: 502 after the second try
        self.state["local/tv"]["url"] = "http://127.0.0.1:9/"
        self.save()
        r = await c.get("/vm/local/tv/mitm/")
        self.assertEqual(r.status, 502)
        self.assertEqual(len([x for x in self.calls() if x.endswith("connect --json local/tv")]), 2)

    async def test_auth_and_hosts(self):
        acc = access.Access("token", "t0ken")
        s, c = await self.client(acc, hosts=("local",), grants=())
        m = self.main
        # the main origin's access first: the inspector's URL is behind it
        self.assertEqual((await m.get("/vm/local/tv/inspector.json")).status, 403)
        # the inspector origin: nothing without the VM's grant, not even with the main token
        for path in ("/vm/local/tv/mitm/", "/vm/local/tv/mitm/flows", "/vm/local/tv/mitm",
                     "/vm/local/tv/mitm/?token=t0ken"):  # fmt: skip
            self.assertEqual((await c.get(path, allow_redirects=False)).status, 403, path)
        with self.assertRaises(Exception) as e:
            await c.ws_connect("/vm/local/tv/mitm/updates")
        self.assertEqual(getattr(e.exception, "status", None), 403)
        r = await m.get("/?token=t0ken", allow_redirects=False)
        self.assertEqual(r.status, 302)
        url = (await (await m.get("/vm/local/tv/inspector.json")).json())["url"]
        self.assertTrue(url.startswith(f"http://127.0.0.1:{c.port}/vm/local/tv/mitm/?grant="), url)
        r = await c.get(url.removeprefix(f"http://127.0.0.1:{c.port}"), allow_redirects=False)
        self.assertEqual((r.status, r.headers["Location"]), (302, "/vm/local/tv/mitm/"))
        (granted,) = [x for x in c.session.cookie_jar if x.key == mitm.GRANT_COOKIE]
        self.assertEqual(granted["path"], "/vm/local/tv/mitm")
        self.assertTrue(granted["httponly"])
        self.assertEqual(granted["samesite"], "Strict")
        self.assertEqual((await c.get("/vm/local/tv/mitm/")).status, 200)
        # the grant is the VM's: another VM's page is refused, as is an old grant
        r = await c.get("/vm/local/off/mitm/")
        self.assertEqual(r.status, 403)
        r = await c.get("/vm/local/off/mitm/?grant=" + s.inspectors.grant("local/tv"))
        self.assertEqual(r.status, 403)
        # --hosts after the grant
        r = await c.get("/vm/pi/tv/mitm/?grant=" + s.inspectors.grant("pi/tv"))
        self.assertEqual(r.status, 403)
        self.assertIn("not served here", await r.text())
        self.assertNotIn("local/tv", " ".join(self.mitmweb.seen[-1].get("Cookie", "")))
        self.assertEqual([x for x in self.calls() if "pi/" in x], [])

    async def test_idle_and_shutdown(self):
        s, c = await self.client(idle=0.3)
        self.assertEqual((await c.get("/vm/local/tv/mitm/flows")).status, 200)
        ws = await c.ws_connect("/vm/local/tv/mitm/updates")
        await ws.receive()
        await asyncio.sleep(0.6)
        self.assertEqual(len(self.connects()), 1, "kept while a WebSocket is open")
        await ws.close()
        await self.wait_for(lambda: not self.connects() and not s.procs)
        self.assertEqual(s.inspectors.upstreams, {})
        # used again: a new one, ended at shutdown
        s, c = await self.client()
        self.assertEqual((await c.get("/vm/local/tv/mitm/flows")).status, 200)
        self.assertEqual(len(self.connects()), 1)
        await c.close()
        await self.main.close()  # the main app's shutdown ends them
        self.assertEqual(s.procs, set())
        await self.wait_for(lambda: not self.connects())
