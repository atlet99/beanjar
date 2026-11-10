#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The mode=mitm namespace (host/core/beanjar/netns.py) end to end, with a fake guest in QEMU's
place (fake-tap-guest: a network namespace of its own on the other end of the TAP) and servers on
this host as the internet. Without a VM, and without internet access.

One netns.mitm() run, as the launcher makes it: the relays, `unshare -rn`, slirp4netns, the
proxy-ARP and AnyIP routing, DHCP and mitmweb with host/helpers/network/mitm_transparent.py. The guest
checks its lease (router and DNS slirp's, the server identifier the TAP's own address) and a
unicast renewal; a server-first and a client-first TCP service on this host reached through slirp
unchanged; a closed port refused; the host's loopback unreachable; HTTP and HTTPS to any address
intercepted by mitmweb (our CA's leaf, the flows in the inspector with their real destination); a
connection to the namespace's own address refused by the proxy rather than looping. The
forwarded port reaches the guest through the relays. No `nft` is run (a failing one is first on
PATH). Everything started is gone afterwards.

Skips (exit 77, a meson SKIP) without unprivileged user namespaces, slirp4netns, ip, or
build/mitmproxy-venv. `meson test -C build/host --suite manager`, or
`BEANJAR_RELAY=... pytest tests/integration/network/test_netns.py`."""

import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import time

import pytest
import urllib.error
import urllib.request

import testunits

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
PKG = os.path.join(TOP, "host", "core")
MITMWEB = os.path.join(TOP, "build", "mitmproxy-venv", "bin", "mitmweb")
PASSWORD = "test-netns-inspector-pw"
SBIN = os.environ.get("PATH", os.defpath) + ":/usr/sbin:/sbin"


def unavailable():
    for tool in ("unshare", "ip", "slirp4netns"):
        if not shutil.which(tool, path=SBIN):
            return f"{tool} not found"
    if not os.access(MITMWEB, os.X_OK):
        return "no build/mitmproxy-venv (scripts/build.sh host)"
    if not os.access("/dev/net/tun", os.R_OK | os.W_OK):
        return "no /dev/net/tun"
    if subprocess.run(["unshare", "-rn", "true"], stderr=subprocess.DEVNULL).returncode != 0:
        return "no unprivileged user namespaces"
    return None


pytestmark = pytest.mark.skipif(bool(unavailable()), reason=unavailable() or "")


# the VM's host address (qemu.py's VM_ADDRESS, 127.0.1.<slot>): the forwards and the inspector bind there
VM_ADDRESS = "127.0.1.250"


def free_port():
    with socket.socket() as s:
        s.bind((VM_ADDRESS, 0))
        return s.getsockname()[1]


def host_address():
    """This host's address towards the internet (no packet is sent), which slirp reaches as any
    other; None if it only has loopback."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("192.0.2.1", 9))
            addr = s.getsockname()[0]
        except OSError:
            return None
    return None if addr.startswith("127.") else addr


class Server:
    """A TCP server on host:0: 'banner' speaks first, 'echo' answers what it is sent."""

    def __init__(self, host, kind):
        self.sock = socket.create_server((host, 0))
        self.port = self.sock.getsockname()[1]
        self.kind = kind
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        while True:
            try:
                c, _ = self.sock.accept()
            except OSError:
                return
            with c:
                c.settimeout(5)
                try:
                    if self.kind == "banner":
                        c.sendall(b"SSH-2.0-fake\r\n")
                    else:
                        c.sendall(b"echo:" + c.recv(100))
                except OSError:
                    pass

    def close(self):
        self.sock.close()


class Run:
    """One netns.mitm() run with its fake guest, and what was observed of it."""

    def __init__(self, addr, tmp):
        self.addr = addr
        self.tmp = str(tmp)
        t = self.tmp
        self.servers = [
            Server(self.addr, "banner"),
            Server(self.addr, "echo"),
            Server("127.0.0.1", "banner"),
        ]
        banner, echo, loopback = (s.port for s in self.servers)
        closed = free_port()
        # a failing nft first on PATH, recording that it ran
        os.makedirs(f"{t}/bin")
        with open(f"{t}/bin/nft", "w") as f:
            f.write(f"#!/bin/sh\ntouch {t}/nft-ran\nexit 1\n")
        os.chmod(f"{t}/bin/nft", 0o755)
        cafile = f"{t}/ca/ca.pem"
        spec = {
            "banner": {"kind": "tcp", "host": self.addr, "port": banner},
            "echo": {"kind": "tcp", "host": self.addr, "port": echo, "send": "ping"},
            "closed": {"kind": "tcp", "host": self.addr, "port": closed},
            "loopback": {"kind": "tcp", "host": "10.0.2.2", "port": loopback},
            "http": {
                "kind": "tcp",
                "host": self.addr,
                "port": 80,
                "send": "GET /probe HTTP/1.1\r\nHost: intercept.test\r\nConnection: close\r\n\r\n",
                "timeout": 30,  # a connection held while mitmweb starts waits (netns._hold)
            },
            "https": {
                "kind": "https",
                "host": self.addr,
                "port": 443,
                "sni": "intercept.test",
                "cafile": cafile,
                "timeout": 30,
            },
            "self": {
                "kind": "tcp",
                "host": "10.0.2.254",
                "port": 80,
                "send": "GET / HTTP/1.1\r\nHost: 10.0.2.254\r\nConnection: close\r\n\r\n",
                "timeout": 30,
            },
        }
        with open(f"{t}/spec.json", "w") as f:
            json.dump(spec, f)
        self.fwd, guest_port, self.web = free_port(), 2222, free_port()
        env = dict(
            os.environ,
            PATH=f"{t}/bin:{SBIN}",
            PYTHONPATH=os.pathsep.join(filter(None, (PKG, os.environ.get("PYTHONPATH")))),
            NETNS_SOCK_DIR=f"{t}/run",
            NETNS_LOG_DIR=t,
            NETSTUB_LOG=f"{t}/netstub.log",
            NETSTUB_CA=f"{t}/ca",
            NETNS_TAP="tap0",
            NETSTUB_WEB_PORT=str(self.web),
            NETNS_FWD=f"{self.fwd}:{guest_port}",
            VM_ADDRESS=VM_ADDRESS,
            FAKE_GUEST_SPEC=f"{t}/spec.json",
            FAKE_GUEST_RESULTS=f"{t}/results.json",
            FAKE_GUEST_STOP=f"{t}/stop",
            FAKE_GUEST_PORT=str(guest_port),
            BEANJAR_MITM_PASSWORD=PASSWORD,
        )
        with open(f"{t}/out.log", "w") as out:
            self.proc = subprocess.Popen(
                [
                    sys.executable,
                    "-c",
                    "import os, sys; from beanjar import netns; "
                    "sys.exit(netns.mitm(sys.argv[1:], os.environ))",
                    sys.executable,
                    os.path.join(TOP, "tests", "fakes", "fake-tap-guest"),
                ],
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=out,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        end = time.monotonic() + 90
        while not os.path.exists(f"{t}/results.json"):
            if self.proc.poll() is not None or time.monotonic() > end:
                self.stop()
                raise AssertionError("the fake guest gave no results:\n" + self.logs())
            time.sleep(0.2)
        with open(f"{t}/results.json") as f:
            self.results = json.load(f)
        # what the host sees while the guest runs: the forward and the inspector's flows
        try:
            with socket.create_connection((VM_ADDRESS, self.fwd), 5) as c:
                self.forwarded = c.recv(100)
        except OSError as e:
            self.forwarded = repr(e).encode()
        try:  # nothing of the VM's on another address
            socket.create_connection(("127.0.0.1", self.fwd), 5).close()
            self.elsewhere = True
        except OSError:
            self.elsewhere = False
        # the inspector: its password file, and mitmweb behind it
        self.password_file = f"{t}/run/mitmweb.password"
        self.password_mode = os.stat(self.password_file).st_mode & 0o777
        with open(self.password_file) as f:
            self.password = f.read().strip()
        url = f"http://{VM_ADDRESS}:{self.web}/flows"
        self.anonymous = self.status(urllib.request.Request(url))
        self.wrong = self.status(urllib.request.Request(url + "?token=wrong"))
        self.token = self.status(urllib.request.Request(url + "?token=" + PASSWORD))
        with urllib.request.urlopen(
            urllib.request.Request(url, headers={"Authorization": f"Bearer {PASSWORD}"}), timeout=10
        ) as r:
            self.flows = json.load(r)
        # netns.mitm's own process was started with BEANJAR_MITM_PASSWORD (as a shell would start
        # it); what it starts must not have it
        self.environ_leaks = [
            pid
            for pid, env in self.environs()
            if PASSWORD.encode() in env and pid != str(self.proc.pid)
        ]
        self.rc = self.stop()

    @staticmethod
    def status(request):
        try:
            with urllib.request.urlopen(request, timeout=10) as r:
                return r.status
        except urllib.error.HTTPError as e:
            return e.code

    def environs(self):
        """(pid, environ) of every process of the run."""
        for pid in filter(str.isdigit, os.listdir("/proc")):
            try:
                with open(f"/proc/{pid}/environ", "rb") as f:
                    env = f.read()
            except OSError:
                continue
            if self.tmp.encode() in env:
                yield pid, env

    def stop(self):
        open(f"{self.tmp}/stop", "w").close()
        try:
            return self.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            testunits.end(self.proc.pid, group=True)
            self.proc.wait()
            return None

    def logs(self):
        out = []
        for name in ("out.log", "netns-uplink.log", "netns-helper.log", "netstub-mitm.log"):
            try:
                with open(os.path.join(self.tmp, name)) as f:
                    out.append(f"--- {name}\n{f.read()[-3000:]}")
            except OSError:
                pass
        return "\n".join(out)

    def close(self):
        for s in self.servers:
            s.close()

    def result(self, name):
        return self.results[name]

    def destinations(self):
        return {
            tuple(f["server_conn"]["address"])
            for f in self.flows
            if f.get("server_conn", {}).get("address")
        }


@pytest.fixture(scope="module")
def run(tmp_path_factory):
    addr = host_address()
    if not addr:
        pytest.skip("no non-loopback host address for the fake internet")
    r = Run(addr, tmp_path_factory.mktemp("test-netns"))
    yield r
    r.close()


def test_exit_and_cleanup(run):
    assert run.rc == 0, run.logs()
    assert not os.path.exists(f"{run.tmp}/run"), "socket dir left behind"
    assert [pid for pid, _ in run.environs()] == [], "processes of the run still alive"


def test_inspector_password(run):
    assert run.password == PASSWORD
    assert run.password_mode == 384
    assert (run.anonymous, run.wrong, run.token) == (403, 403, 200)
    assert run.environ_leaks == [], "the password in a process environment"
    assert not os.path.exists(run.password_file)
    for name in ("out.log", "netstub-mitm.log", "netstub.log"):
        with open(os.path.join(run.tmp, name)) as f:
            assert PASSWORD not in f.read(), name


def test_no_nft(run):
    assert not os.path.exists(f"{run.tmp}/nft-ran")


def test_dhcp(run):
    d = run.result("dhcp")
    assert (d["offer"], d["ack"], d["mask"], d["router"], d["dns"], d["server"]) == (
        "10.0.2.15",
        "10.0.2.15",
        "255.255.255.0",
        "10.0.2.2",
        "10.0.2.3",
        "10.0.2.254",
    ), d
    assert (d["renew"], d["renew_server"]) == ("10.0.2.15", "10.0.2.254"), d


def test_other_tcp_through_slirp(run):
    assert run.result("banner").get("data") == "SSH-2.0-fake\r\n", run.results
    assert run.result("echo").get("data") == "echo:ping", run.results


def test_closed_port_refused(run):
    assert run.result("closed").get("error") == "ConnectionRefusedError", run.results


def test_host_loopback_unreachable(run):
    assert "data" not in run.result("loopback"), run.results


def test_http_intercepted(run):
    data = run.result("http").get("data", "")
    assert data.startswith("HTTP/1.1 "), run.results
    assert (run.addr, 80) in run.destinations()


def test_https_intercepted_with_our_ca(run):
    r = run.result("https")
    assert r.get("issuer") == "Beanjar netstub local CA", r
    assert r.get("data", "").startswith("HTTP/1.1 "), r
    assert (run.addr, 443) in run.destinations()


def test_own_address_refused_not_looping(run):
    r = run.result("self")
    assert r.get("data", "").startswith("HTTP/1.1 502"), r
    assert r["seconds"] < 5, r


def test_forwarded_port(run):
    assert run.forwarded == b"guest-banner\n"
    assert not run.elsewhere, "the forward is on the VM's address only"


def test_mitm_log(run):
    with open(f"{run.tmp}/netstub-mitm.log") as f:
        log = f.read()
    assert "destination unknown" not in log
    assert "Transparent mode failure" not in log
