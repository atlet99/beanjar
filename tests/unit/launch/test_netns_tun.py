# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""netns.py's inspector: the policy rules and route into mitmweb's TUN device, mitmweb's command
line, the run's mitmweb.json, and the start without a usable mitmweb."""

import os
import socket
from types import SimpleNamespace
from unittest import mock

from beanjar import mitmtool, netns


class Tree:
    def src(self, *p):
        return "/src/" + "/".join(p)


def test_rules_take_all_tcp_and_udp_from_the_tap_only():
    rules = netns.intercept_rules("tap0", "add")
    assert rules == [
        ["rule", "add", "pref", "100", "iif", "tap0", "ipproto", "tcp", "lookup", "101"],
        ["rule", "add", "pref", "100", "iif", "tap0", "ipproto", "udp", "lookup", "101"],
    ]
    # every rule is the guest's (iif the TAP): mitmweb's own upstream is not matched
    assert all(r[r.index("iif") + 1] == "tap0" for r in rules)
    # nothing else (ICMP) is: it is forwarded to slirp
    assert {r[r.index("ipproto") + 1] for r in rules} == {"tcp", "udp"}
    assert netns.hold_rules("tap0", "add") == [
        ["rule", "add", "pref", "101", "iif", "tap0", "ipproto", "tcp", "blackhole"],
        ["rule", "add", "pref", "101", "iif", "tap0", "ipproto", "udp", "blackhole"],
    ]
    assert netns.tun_route("replace") == [
        "route", "replace", "default", "dev", "mitm0", "table", "101",
    ]  # fmt: skip


def test_intercept_adds_the_route_before_the_rules():
    calls = []
    with mock.patch.object(netns, "_ip", lambda env, *a: calls.append(list(a))):
        netns._intercept({}, "tap0", "add")
    assert calls[0] == netns.tun_route("replace")
    assert calls[1:] == netns.intercept_rules("tap0", "add")
    calls.clear()
    with mock.patch.object(netns, "_ip", lambda env, *a: calls.append(list(a))):
        netns._intercept({}, "tap0", "del")
    assert calls == netns.intercept_rules("tap0", "del")  # the route goes with the device


def test_argv():
    env = {"MITM_FLOWS": "50"}
    u = netns.mitmweb_argv("/opt/pipx/bin/mitmweb", Tree(), env, "/ca", "18083")
    assert u[:3] == ["/opt/pipx/bin/mitmweb", "--mode", "tun:mitm0"]
    assert "--listen-host" not in u
    assert u[-2:] == ["-s", "/src/host/helpers/network/mitm_addon.py"]
    assert u.count("-s") == 1
    assert "connection_strategy=eager" in u  # server-first protocols on any port
    for a in ("confdir=/ca", "ssl_insecure=true", "max_flows=50", "max_flow_bytes=0"):
        assert a in u
    assert u[u.index("--web-host") + 1] == "127.0.0.1" and u[u.index("--web-port") + 1] == "18083"


def test_argv_default_flows():
    from beanjar import vmsettings

    u = netns.mitmweb_argv("/m", Tree(), {}, "/ca", "1")
    assert f"max_flows={vmsettings.MITM_FLOWS}" in u and vmsettings.MITM_FLOWS == 5000


def test_info_file(tmp_path):
    info = {"version": "12.3.1", "path": "/m", "problem": ""}
    netns._write_info({"RUN_DIR": str(tmp_path)}, "/unused", info)
    assert netns.mitm_info(str(tmp_path)) == info
    assert netns.mitm_info(str(tmp_path / "none")) is None
    netns._write_info({}, None, info)  # nowhere to write: nothing, no error


def test_ready_waits_for_the_device(tmp_path):
    ready = tmp_path / "ready"
    ready.write_text("1\n")
    process = SimpleNamespace(poll=lambda: None)
    p = netns.Proxy(process, str(ready), "1", "log")
    with (
        mock.patch.object(netns, "_can_connect", return_value=True),
        mock.patch.object(socket, "if_nametoindex", side_effect=OSError),
    ):
        assert p.poll() is None
    with (
        mock.patch.object(netns, "_can_connect", return_value=True),
        mock.patch.object(socket, "if_nametoindex", return_value=7),
    ):
        assert p.poll() is True


def test_unit_keeps_the_mitmweb_override():
    from beanjar import unit

    assert unit.env_for({"NETNS_MITMWEB": "/x/mitmweb"}) == {"NETNS_MITMWEB": "/x/mitmweb"}


def test_without_mitmweb_routes_nothing(tmp_path, capsys):
    """No mitmweb: no Proxy, so inner() adds no hold and no rule (the guest goes straight out),
    and the reason, with how to install it, is logged and recorded."""
    env = {"PATH": str(tmp_path), "RUN_DIR": str(tmp_path)}
    with mock.patch.dict(os.environ, {"HOME": str(tmp_path)}):
        assert netns._start_proxy(env, Tree(), str(tmp_path), str(tmp_path), "1") is None
    err = capsys.readouterr().err
    assert "forwarding directly" in err and mitmtool.INSTALL in err
    info = netns.mitm_info(str(tmp_path))
    assert info["path"] == "" and mitmtool.INSTALL in info["problem"]


def test_too_old_routes_nothing(tmp_path, capsys):
    exe = tmp_path / "mitmweb"
    exe.write_text("#!/bin/sh\necho 'Mitmproxy: 11.1.3'\n")
    exe.chmod(0o755)
    env = {"NETNS_MITMWEB": str(exe), "RUN_DIR": str(tmp_path)}
    assert netns._start_proxy(env, Tree(), str(tmp_path), str(tmp_path), "1") is None
    assert "NETNS_MITM_READY" not in env  # never started
    assert "11.1.3" in capsys.readouterr().err
    info = netns.mitm_info(str(tmp_path))
    assert info["version"] == "11.1.3" and "too old" in info["problem"]
