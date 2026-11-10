#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Optional proxy lifecycle, including real AnyIP withdrawal and guest HTTP traffic: with no proxy
and with one that dies before it listens, the guest's HTTP goes directly; with one that listens
only after the guest's first connection was opened ("late"), that connection is held (its SYN
dropped and sent again) and then answered by the proxy, neither refused nor reset."""

import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

TOP = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(TOP / "host/core"))
from beanjar import netns, paths


class StopHandler(unittest.TestCase):
    def test_second_signal_does_not_interrupt_cleanup(self):
        old = {s: signal.getsignal(s) for s in (signal.SIGTERM, signal.SIGINT)}
        stops = []
        done = []
        try:
            netns._raise_on(signal.SIGTERM, signal.SIGINT)
            try:
                os.kill(os.getpid(), signal.SIGTERM)
                self.fail("first signal did not raise")
            except netns._Stop as e:
                stops.append(e.sig)
            finally:
                os.kill(os.getpid(), signal.SIGTERM)  # arrives before SIG_IGN is installed
                os.kill(os.getpid(), signal.SIGINT)
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
                done.append(True)
        finally:
            for s, h in old.items():
                signal.signal(s, h)
        self.assertEqual(stops, [signal.SIGTERM])
        self.assertEqual(done, [True])


class OptionalProxy(unittest.TestCase):
    def test_missing_proxy_does_not_generate_ca(self):
        tree = mock.Mock()
        tree.mitmweb.return_value = "/nonexistent/beanjar-mitmweb"
        with mock.patch.object(netns.subprocess, "run") as run:
            self.assertIsNone(netns._start_proxy({}, tree, "/unused", "/unused", "18083"))
        run.assert_not_called()

    def test_start_does_not_wait(self):
        """_start_proxy returns at once, whatever mitmweb does; a Proxy whose mitmweb exited
        before it listened is not ready (False), one still starting is pending (None)."""
        with tempfile.TemporaryDirectory() as directory:
            tree = mock.Mock()
            tree.mitmweb.return_value = sys.executable
            tree.src.return_value = "/unused"
            tree.build.return_value = directory + "/flows"
            process = mock.Mock()
            process.poll.return_value = None
            with (
                mock.patch.object(
                    netns.subprocess, "run", return_value=mock.Mock(stdout=directory + "/ca.pem")
                ),
                mock.patch.object(netns.subprocess, "Popen", return_value=process),
            ):
                proxy = netns._start_proxy({}, tree, directory, directory, "1")
            self.assertIs(proxy.process, process)
            self.assertIsNone(proxy.poll())
            process.poll.return_value = 1
            self.assertIs(proxy.poll(), False)

    def proxy(self, ready):
        p = netns.Proxy(mock.Mock(), "/nonexistent/ready", "1", "/unused/log")
        p.ready = ready
        return p

    def test_never_ready_proxy_is_reaped_and_releases_the_hold(self):
        guest = mock.Mock()
        guest.wait.side_effect = [subprocess.TimeoutExpired("guest", 0.2), 7]
        proxy = self.proxy(False)
        proxy.process.poll.return_value = 1
        with mock.patch.object(netns, "_ip") as ip:
            self.assertEqual(netns._wait_guest(guest, proxy, {}, "tap0"), 7)
        self.assertEqual(
            [c.args[1:4] for c in ip.call_args_list],
            [("rule", "del", "pref")] * len(netns.PROXIED),
        )
        self.assertTrue(all(c.args[4] == netns.HOLD_PREF for c in ip.call_args_list))
        proxy.process.wait.assert_called()
        guest.terminate.assert_not_called()

    def test_ready_then_dead_proxy_intercepts_then_withdraws(self):
        """Ready: interception added before the hold is withdrawn; its exit withdraws interception."""
        guest = mock.Mock()
        guest.wait.side_effect = [subprocess.TimeoutExpired("guest", 0.2)] * 2 + [7]
        proxy = self.proxy(True)
        proxy.process.poll.side_effect = [1, 1, 1]
        with mock.patch.object(netns, "_ip") as ip:
            self.assertEqual(netns._wait_guest(guest, proxy, {}, "tap0"), 7)
        steps = [(c.args[2], c.args[4]) for c in ip.call_args_list]
        n = len(netns.PROXIED)
        self.assertEqual(
            steps,
            [("add", netns.INTERCEPT_PREF)] * n
            + [("del", netns.HOLD_PREF)] * n
            + [("del", netns.INTERCEPT_PREF)] * n,
        )
        guest.terminate.assert_not_called()

    def test_network_rule_errors_remain_fatal(self):
        with mock.patch.object(netns, "_ip", side_effect=netns.Error("routing failed")):
            with self.assertRaisesRegex(netns.Error, "routing failed"):
                netns._intercept({}, "tap0", "add")


# This probe runs in its own user/network namespace and uses the existing TAP guest.
# No host interfaces, port bindings or installed dependencies are changed.
PROBE = r'''
import http.server, json, os, pathlib, subprocess, sys, threading, time
from unittest import mock
from beanjar import netns, paths
root, directory, mode = sys.argv[1:]
env = os.environ
def ip(*args): subprocess.run(['ip', *args], check=True)
# Put the HTTP server beyond a real uplink. Leaving an AnyIP rule installed would
# deliver guest port 80 to this namespace, where no listener exists, and fail.
server_code = """
import http.server, os, pathlib, subprocess, sys, time
os.unshare(os.CLONE_NEWNET)
pathlib.Path(sys.argv[1], 'server-ns').touch()
while subprocess.run(['ip', 'link', 'show', 'server0'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode: time.sleep(.02)
for args in [('link','set','lo','up'), ('addr','add','192.0.2.1/24','dev','server0'), ('link','set','server0','up'), ('route','add','default','via','192.0.2.2')]: subprocess.run(['ip',*args], check=True)
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.end_headers(); self.wfile.write(b'direct-ok')
    def log_message(self, *args): pass
server = http.server.HTTPServer(('192.0.2.1', 80), Handler)
pathlib.Path(sys.argv[1], 'server-ready').touch()
server.serve_forever()
"""
server = subprocess.Popen([sys.executable, '-c', server_code, directory], preexec_fn=netns._die_with_parent)
while not pathlib.Path(directory, 'server-ns').exists():
    assert server.poll() is None
    time.sleep(.02)
ip('link', 'add', 'up0', 'type', 'veth', 'peer', 'name', 'server0')
ip('link', 'set', 'server0', 'netns', str(server.pid))
ip('addr', 'add', '192.0.2.2/24', 'dev', 'up0')
ip('link', 'set', 'up0', 'up')
ip('route', 'add', 'default', 'via', '192.0.2.1')
while not pathlib.Path(directory, 'server-ready').exists():
    assert server.poll() is None
    time.sleep(.02)
spec = {'http': {'kind': 'tcp', 'host': '192.0.2.1', 'port': 80, 'send': 'GET / HTTP/1.0\r\n\r\n'}}
pathlib.Path(directory, 'spec').write_text(json.dumps(spec))
env.update(NETNS_RUNDIR=directory, NETNS_LOG_DIR=directory, NETNS_TAP='tap0',
           FAKE_GUEST_SPEC=directory+'/spec', FAKE_GUEST_RESULTS=directory+'/results',
           FAKE_GUEST_STOP=directory+'/stop', FAKE_GUEST_PORT='2222')
proxy = None
if mode == 'late':
    # mitmweb's stand-in: it listens on port 80 (where AnyIP delivers the intercepted guest's
    # traffic) and its web port only 1.5 s after the guest has its lease and starts its probe, so
    # that probe's SYN arrives while the proxy is not up yet and is held (netns._hold).
    late = """
import http.server, os, pathlib, socket, sys, threading, time
directory = sys.argv[1]
while not pathlib.Path(directory, 'leased').exists(): time.sleep(.02)
time.sleep(1.5)
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.end_headers(); self.wfile.write(b'proxy-ok')
    def log_message(self, *args): pass
class Server(http.server.HTTPServer):
    def server_bind(self):
        # as mitm_transparent.py has mitmweb's: answer from the address the guest dialled
        self.socket.setsockopt(socket.SOL_IP, 19, 1)  # IP_TRANSPARENT
        super().server_bind()
server = Server(('0.0.0.0', 80), Handler)
web = socket.create_server(('127.0.0.1', 18099))
pathlib.Path(directory, 'mitmweb.ready').touch()
pathlib.Path(directory, 'ready-at').write_text(str(time.monotonic()))
server.serve_forever()
"""
    process = subprocess.Popen([sys.executable, '-c', late, directory], preexec_fn=netns._die_with_parent)
    proxy = netns.Proxy(process, directory + '/mitmweb.ready', '18099', directory + '/late.log')
    env['FAKE_GUEST_LEASED'] = directory + '/leased'
    spec['http']['timeout'] = 20
    pathlib.Path(directory, 'spec').write_text(json.dumps(spec))
elif mode == 'dead':
    proxy = netns.Proxy(subprocess.Popen(['sleep', '60'], preexec_fn=netns._die_with_parent),
                        directory + '/never-ready', '1', directory + '/dead.log')
    # The guest starts only after inner has installed its hold rules. Its wrapper kills the
    # proxy, which never got ready, and allows the monitor to remove the rules before making
    # requests: they go directly.
    wrapper = 'import os, runpy, signal, subprocess, time; rules = subprocess.check_output(["ip", "rule"], text=True); assert "dport 80" in rules and "dport 443" in rules, rules; os.kill(%d, signal.SIGTERM); time.sleep(0.6); runpy.run_path(%r, run_name="__main__")' % (proxy.process.pid, root+'/tests/fakes/fake-tap-guest')
if mode != 'dead':
    wrapper = 'import runpy; runpy.run_path(%r, run_name="__main__")' % (root+'/tests/fakes/fake-tap-guest')
def stop_when_done():
    while not pathlib.Path(directory, 'results').exists(): time.sleep(.05)
    pathlib.Path(directory, 'stop').touch()
threading.Thread(target=stop_when_done, daemon=True).start()
try:
    with mock.patch.object(netns, '_start_proxy', return_value=proxy):
        status = netns.inner([sys.executable, '-c', wrapper])
    result = json.loads(pathlib.Path(directory, 'results').read_text())
    assert status == 0, status
    assert '200 OK' in result['http'].get('data', ''), result
    if mode == 'late':
        # held, not refused or reset: it waited for the proxy (TCP sent its SYN again) and was
        # answered by it, not by the server beyond the uplink
        assert 'proxy-ok' in result['http']['data'], result
        assert result['http']['seconds'] >= 1.0, result
    else:
        assert 'direct-ok' in result['http']['data'], result
    rules = subprocess.check_output(['ip', 'rule'], text=True)
    assert 'blackhole' not in rules, rules  # the hold is gone either way
    if mode == 'late':  # intercepting still (the namespace ends with the VM)
        assert 'dport 80 lookup' in rules and 'dport 443 lookup' in rules, rules
    else:
        assert 'dport 80' not in rules and 'dport 443' not in rules, rules
    print('http', result['http']['seconds'], 's', file=sys.stderr)
finally:
    server.terminate()
    server.wait()
    if proxy is not None:
        if proxy.process.poll() is None: proxy.process.terminate()
        proxy.process.wait()
'''


class RealRouting(unittest.TestCase):
    def test_direct_http_without_proxy_and_after_proxy_death_held_while_it_starts(self):
        for tool in ("unshare", "ip"):
            if not shutil.which(tool):
                self.skipTest(tool + " unavailable")
        if not os.access("/dev/net/tun", os.R_OK | os.W_OK):
            self.skipTest("TUN unavailable")
        if subprocess.run(["unshare", "-rn", "true"], stderr=subprocess.DEVNULL).returncode:
            self.skipTest("unprivileged namespaces unavailable")
        for mode in os.environ.get("MODES", "missing dead late").split():
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                env = dict(os.environ, PYTHONPATH=str(TOP / "host/core"))
                process = subprocess.Popen(
                    ["unshare", "-rn", sys.executable, "-c", PROBE, str(TOP), directory, mode],
                    env=env,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    start_new_session=True,
                )
                try:
                    stdout, stderr = process.communicate(timeout=60)
                    self.assertEqual(process.returncode, 0, stdout + stderr)
                    if os.environ.get("VERBOSE"):
                        print(stderr, file=sys.stderr)
                finally:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.communicate()


if __name__ == "__main__":
    unittest.main()
