#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-web's start and stop (host/web/beanjar_web/server.py vm_action): the stop dialog's two
choices reach the manager as `beanjar stop` (the power-off signal, with its fallback) and
`beanjar stop --unplug`, each with --force when the page has shown the reason, for this machine's
VMs and another host's alike; the operation says which it is. The manager is stood in by
Server.spawn, a process that exits at once."""

import asyncio
import os
import sys
import unittest

import pytest

pytest.importorskip("aiohttp", reason="python3-aiohttp is not installed")
from aiohttp.test_utils import TestClient, TestServer  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../host/web"))
from beanjar_web import access, server  # noqa: E402

STATIC = os.path.dirname(os.path.abspath(__file__))  # any directory: no static file is asked


class Exited:
    """A manager process that has printed its stderr and exited with returncode."""

    def __init__(self, returncode=0, err=b""):
        self.pid = 0
        self.returncode = returncode
        self.stdout, self.stderr = asyncio.StreamReader(), asyncio.StreamReader()
        self.stdout.feed_eof()
        self.stderr.feed_data(err)
        self.stderr.feed_eof()

    async def wait(self):
        return self.returncode


class Actions(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.s = server.Server(["beanjar"], access.Access("none"), STATIC, STATIC)
        self.calls = []
        self.result = (0, b"")

        async def spawn(words, stdin=False):
            self.calls.append(list(words))
            return Exited(*self.result)

        self.s.spawn = spawn
        self.c = TestClient(TestServer(self.s.app()))
        await self.c.start_server()
        self.addAsyncCleanup(self.c.close)

    async def finished(self, path):
        """The operation of a VM once the manager has returned."""
        for _ in range(100):
            r = await self.c.get(f"{path}/operation")
            if r.status != 202:
                return r.status, await r.json()
            await asyncio.sleep(0.01)
        self.fail("the operation stayed pending")

    async def stop(self, path, query=""):
        r = await self.c.post(f"{path}/stop{query}")
        self.assertEqual(r.status, 202)
        return await r.json(), await self.finished(path)

    async def test_power_off(self):
        accepted, (status, done) = await self.stop("/vm/local/tv")
        self.assertEqual(accepted["message"], "Stopping…")
        self.assertNotIn("unplug", accepted)
        self.assertEqual(status, 200)
        self.assertEqual(self.calls, [["stop", "--json", "local/tv"]])

    async def test_unplug(self):
        accepted, (status, done) = await self.stop("/vm/local/tv", "?unplug=1")
        self.assertEqual(accepted["message"], "Unplugging…")
        self.assertTrue(accepted["unplug"])
        self.assertEqual((status, done["status"], done["action"]), (200, "succeeded", "stop"))
        self.assertEqual(self.calls, [["stop", "--json", "--unplug", "local/tv"]])

    async def test_unplug_with_reason_shown(self):
        await self.stop("/vm/local/tv", "?force=1&unplug=1")
        self.assertEqual(self.calls, [["stop", "--json", "--force", "--unplug", "local/tv"]])

    async def test_unplug_another_host(self):
        # a VM of another host listed on the page: the same CLI, HOST/NAME
        await self.stop("/vm/pi/w16r", "?unplug=1")
        self.assertEqual(self.calls, [["stop", "--json", "--unplug", "pi/w16r"]])

    async def test_unplug_only_for_stop(self):
        r = await self.c.post("/vm/local/tv/start?unplug=1&force=1")
        self.assertEqual(r.status, 202)
        await self.finished("/vm/local/tv")
        self.assertEqual(self.calls, [["start", "--json", "local/tv"]])

    async def test_other_values_are_not_unplug(self):
        await self.stop("/vm/local/tv", "?unplug=yes")
        self.assertEqual(self.calls, [["stop", "--json", "local/tv"]])

    async def test_refusal(self):
        self.result = (1, b'{"error": "tv runs for a reason: T31 check"}')
        _, (status, done) = await self.stop("/vm/local/tv", "?unplug=1")
        self.assertEqual((status, done["status"]), (409, "failed"))
        self.assertIn("T31 check", done["message"])

    async def test_one_at_a_time(self):
        gate = asyncio.Event()
        spawn = self.s.spawn

        async def slow(words, stdin=False):
            await gate.wait()
            return await spawn(words, stdin)

        self.s.spawn = slow
        r = await self.c.post("/vm/local/tv/stop?unplug=1")
        self.assertEqual(r.status, 202)
        r = await self.c.post("/vm/local/tv/stop")
        self.assertEqual(r.status, 409)
        self.assertIn("still in progress", (await r.json())["error"])
        gate.set()
        await self.finished("/vm/local/tv")
        self.assertEqual(self.calls, [["stop", "--json", "--unplug", "local/tv"]])
