#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Grouped VM schema and lossless edits."""

from unittest import mock

import pytest

from beanjar import core, paths, vmconfig, vmidentity, vmsettings


@pytest.fixture
def path(tmp_path):
    return tmp_path / "vm.toml"


def test_defaults(path):
    path.write_text('[firmware]\ndirectory = "firmware"\n')
    cfg = vmconfig.read(path)
    assert cfg == dict(
        smp=4,
        display="dbus",
        audio="dbus",
        link="nat",
        mode="mitm",
        layers=["normal", "gl"],
    )
    vmconfig.write(path, cfg)
    assert path.read_text() == '[firmware]\ndirectory = "firmware"\n'
    assert (
        vmconfig.decode({"firmware": {"directory": "../fw"}, "display": {"backend": "none"}})[
            "audio"
        ]
        == "none"
    )


def test_gpu(path):
    """display.gpu: the launcher's GPU_DEVICE; auto, the default, is not written."""
    path.write_text('[firmware]\ndirectory = "firmware"\n\n[display]\ngpu = "virtio-gpu-gl"\n')
    cfg = vmconfig.read(path)
    assert cfg["gpu"] == "virtio-gpu-gl"
    assert vmsettings.SETTINGS["gpu"] == "GPU_DEVICE"


def test_validation():
    bad = [
        ({"hardware": {"cpus": True}}, "hardware.cpus"),
        ({"hardware": {"memory_mib": 0}}, "hardware.memory_mib"),
        ({"hardware": {"memory": 1024}}, "hardware.memory"),
        ({"display": {"backend": False}}, "display.backend"),
        ({"platform": "webOS"}, "unknown section"),
        ({"advanced": {"env": {"RUN_DIR": "/x"}}}, "owned by the launcher"),
        ({"advanced": {"env": {"PMLOG_DEBUG": True}}}, "string, number"),
        ({"layers": {"enabled": ["../bad"]}}, "layers.enabled"),
        ({"network": {"link": "bridge"}}, "network.link"),
        ({"hardware": {"free_page_reporting": "yes"}}, "hardware.free_page_reporting"),
        ({"display": {"gpu": "on"}}, "display.gpu"),
        ({"display": {"gpu": True}}, "display.gpu"),
        ({"advanced": {"env": {"GPU_DEVICE": "virtio-gpu-gl"}}}, "owned by the launcher"),
        ({"hardware": {"free_page_reporting": True}}, "hardware.free_page_reporting"),
        ({"network": {"mitm_flows": -1}}, "network.mitm_flows"),
        ({"network": {"mitm_flows": "500"}}, "network.mitm_flows"),
        ({"network": {"mitm_flows": True}}, "network.mitm_flows"),
        ({"network": {"mitm_flow_bytes": "lots"}}, "network.mitm_flow_bytes"),
        ({"network": {"mitm_flow_bytes": 200}}, "network.mitm_flow_bytes"),
        ({"network": {"mode": "off"}}, "network.link"),
        ({"network": {"link": "nat", "mode": "off"}}, "network.mode"),
        ({"network": {"net": "mitm"}}, "unknown setting network.net"),
        (
            {"network": {"link": "lan", "listen": "0.0.0.0"}},
            "network.listen: listen is for link=nat",
        ),
        ({"network": {"address": "dhcp"}}, "network.address: address is for link=lan"),
        ({"network": {"link": "off", "address": "dhcp"}}, "address is for link=lan"),
        ({"network": {"link": "lan", "address": "10.0.0.5"}}, "network.address"),
        ({"network": {"link": "lan", "address": "10.0.0.5/30,10.0.0.1"}}, "network.address"),
        ({"network": {"link": "lan", "address": "10.0.0.5/24,10.0.0.5"}}, "network.address"),
    ]
    for fields, message in bad:
        with pytest.raises(core.Error, match=message):
            vmconfig.decode(dict(firmware={"directory": "firmware"}, **fields))
    with pytest.raises(core.Error):
        vmconfig.decode({})
    with pytest.raises(core.Error, match="unknown section"):
        vmconfig.decode({"smp": 4})


def test_userland():
    """hardware.userland: native, the default, is not written out; qemu-user is USERLAND=qemu-user
    and is refused on the platforms that cannot run it (vmsettings.USERLAND_REFUSED)."""
    base = {"firmware": {"directory": "firmware"}}
    assert "userland" not in vmconfig.decode(base)
    assert "USERLAND" not in vmsettings.launcher_env(vmconfig.decode(base))
    cfg = vmconfig.decode(dict(base, hardware={"userland": "qemu-user"}))
    assert vmsettings.launcher_env(cfg)["USERLAND"] == "qemu-user"
    vmsettings.validate(cfg)
    vmsettings.validate(dict(cfg, platform="webOS"))
    vmsettings.validate(dict(cfg, platform="Test"))
    for platform in ("NetCast", "SimpleSmart"):
        with pytest.raises(core.Error, match="userland=qemu-user"):
            vmsettings.validate(dict(cfg, platform=platform))
    vmsettings.validate(dict(cfg, userland="native", platform="NetCast"))
    with pytest.raises(core.Error, match="userland=x86"):
        vmsettings.validate(dict(cfg, userland="x86"))
    assert vmsettings.SETTINGS["userland"] == "USERLAND"


def test_free_page_reporting():
    """Opt-in: off is the default and is not written out; on is FREE_PAGE_REPORTING=on."""
    base = {"firmware": {"directory": "firmware"}}
    assert "free_page_reporting" not in vmconfig.decode(base)
    cfg = vmconfig.decode(dict(base, hardware={"free_page_reporting": "on"}))
    assert vmsettings.launcher_env(cfg)["FREE_PAGE_REPORTING"] == "on"
    assert "FREE_PAGE_REPORTING" not in vmsettings.launcher_env(vmconfig.decode(base))


def test_debug_mode():
    """firmware.debug_mode: debug (the default, not written out), event or release; DEBUG_MODE."""
    base = {"firmware": {"directory": "firmware"}}
    assert "debug_mode" not in vmconfig.decode(base)
    assert "DEBUG_MODE" not in vmsettings.launcher_env(vmconfig.decode(base))
    cfg = vmconfig.decode(dict(base, firmware={"directory": "firmware", "debug_mode": "release"}))
    assert vmsettings.launcher_env(cfg)["DEBUG_MODE"] == "release"
    for bad, why in (("quiet", "one of debug, event, release"), (5, "must be a string")):
        with pytest.raises(core.Error, match=why):
            vmconfig.decode(dict(base, firmware={"directory": "firmware", "debug_mode": bad}))


def test_broadcast(path, tmp_path):
    """firmware.broadcast: the broadcast standard a global firmware boots as (BROADCAST), one of
    dvb, atsc and arib, and one the VM's chip has a board for where its manifest tells the chip."""
    base = {"firmware": {"directory": "firmware"}}
    assert "broadcast" not in vmconfig.decode(base)
    cfg = vmconfig.decode(dict(base, firmware={"directory": "firmware", "broadcast": "atsc"}))
    assert vmsettings.launcher_env(cfg)["BROADCAST"] == "atsc"
    for bad, why in (("pal", "one of dvb, atsc, arib"), (5, "must be a string")):
        with pytest.raises(core.Error, match=why):
            vmconfig.decode(dict(base, firmware={"directory": "firmware", "broadcast": bad}))
    fw = tmp_path / "firmware"
    fw.mkdir()
    manifest = fw / "manifest.toml"
    path.write_text('[firmware]\ndirectory = "firmware"\nbroadcast = "atsc"\n')
    assert vmconfig.read(path)["broadcast"] == "atsc"  # no manifest: the launcher's
    manifest.write_text('name = "x"\nchip = "o22"\nrelease = "queue"\n')
    assert vmconfig.read(path)["broadcast"] == "atsc"
    manifest.write_text('name = "x"\nchip = "lm21ann"\nrelease = "number1"\n')
    with pytest.raises(
        core.Error, match="firmware.broadcast: lm21ann-number1 has no board for broadcast"
    ):
        vmconfig.read(path)
    manifest.write_text('name = "x"\nchip = "o22"\nrelease = "queue"\n')
    (fw / "otncabi-arib.pak").touch()
    assert vmconfig.read(path)["broadcast"] == "atsc"  # pak names state no standard
    # an ARIB firmware: its model_id.json says so
    with mock.patch.object(paths, "model_id", return_value={"region": "ARIB"}):
        with pytest.raises(core.Error, match="broadcast 'atsc', but the firmware is 'arib'"):
            vmconfig.read(path)


def test_mitm_flows():
    """The inspector's flow history: 1000 by default, not written out; 0 keeps all."""
    base = {"firmware": {"directory": "firmware"}}
    assert "MITM_FLOWS" not in vmsettings.launcher_env(vmconfig.decode(base))
    for n in (0, 200):
        cfg = vmconfig.decode(dict(base, network={"mitm_flows": n}))
        assert vmsettings.launcher_env(cfg)["MITM_FLOWS"] == str(n)


def test_forward():
    """network.forward is FORWARD; unset, none; not a string, or a bad port, refused."""
    base = {"firmware": {"directory": "firmware"}}
    assert "FORWARD" not in vmsettings.launcher_env(vmconfig.decode(base))
    cfg = vmconfig.decode(dict(base, network={"forward": "8080,8081:80"}))
    assert vmsettings.launcher_env(cfg)["FORWARD"] == "8080,8081:80"
    for bad in (8080, "80", "x"):
        with pytest.raises(core.Error, match="forward"):
            vmconfig.decode(dict(base, network={"forward": bad}))


def test_mitm_flow_bytes():
    """The inspector's byte budget: off by default; a size such as 200M is MITM_FLOW_BYTES."""
    base = {"firmware": {"directory": "firmware"}}
    assert "MITM_FLOW_BYTES" not in vmsettings.launcher_env(vmconfig.decode(base))
    cfg = vmconfig.decode(dict(base, network={"mitm_flow_bytes": "200M"}))
    assert vmsettings.launcher_env(cfg)["MITM_FLOW_BYTES"] == "200M"
    assert [vmsettings.byte_size(x) for x in ("200M", "1g", "512K", "4096", "", "0")] == [
        200 << 20,
        1 << 30,
        512 << 10,
        4096,
        0,
        0,
    ]
    for bad in ("2T", "-1", "1.5M", "M"):
        with pytest.raises(ValueError):
            vmsettings.byte_size(bad)
    assert (vmsettings.flow_bytes_label("200M"), vmsettings.flow_bytes_label(None)) == (
        "200 MiB",
        "no limit",
    )


def test_network():
    base = {"firmware": {"directory": "firmware"}}
    cfg = vmconfig.decode(
        dict(
            base,
            network={
                "link": "lan",
                "mode": "direct",
                "address": "192.168.1.50/24,192.168.1.1",
            },
        )
    )
    assert (cfg["link"], cfg["mode"], cfg["address"]) == (
        "lan",
        "direct",
        "192.168.1.50/24,192.168.1.1",
    )
    assert (
        vmconfig.decode(dict(base, network={"link": "lan", "address": "dhcp"}))["address"] == "dhcp"
    )
    assert vmconfig.decode(dict(base, network={"link": "off", "mode": "direct"}))["link"] == "off"
    assert (
        vmconfig.decode(dict(base, network={"link": "nat", "listen": "0.0.0.0"}))["listen"]
        == "0.0.0.0"
    )
    assert vmsettings.launcher_env(dict(link="lan", mode="direct", address="dhcp")) == {
        "NET_LINK": "lan",
        "NET_MODE": "direct",
        "NET_ADDRESS": "dhcp",
    }


def test_lan_parent(tmp_path):
    sys_net = tmp_path / "net"
    for name in ("eth0", "wlan0/wireless", "lo"):
        (sys_net / name).mkdir(parents=True)
    assert vmidentity.parse_lan_parent(" eth0 ", str(sys_net)) == "eth0"
    for value, message in (
        ("wlan0", "wireless"),
        ("eth1", "no such"),
        ("lo", "loopback"),
        ("../eth0", "interface name"),
        ("a" * 16, "interface name"),
    ):
        with pytest.raises(core.Error, match=message):
            vmidentity.parse_lan_parent(value, str(sys_net))


def test_edit_and_failed_edit(path):
    original = '# user note\n[firmware]\ndirectory="../firmware" # location\n\n[hardware] # machine\ncpus=4 # keep this\n\n[advanced]\nenv = { PMLOG_DEBUG = "1" } # debugging\n'
    path.write_text(original)
    cfg = vmconfig.read(path)
    vmconfig.write(path, cfg)
    assert path.read_text() == original
    cfg["smp"] = 2
    vmconfig.write(path, cfg)
    expected = original.replace("cpus=4", "cpus=2")
    assert path.read_text() == expected
    cfg["smp"] = True
    with pytest.raises(core.Error):
        vmconfig.write(path, cfg)
    assert path.read_text() == expected
