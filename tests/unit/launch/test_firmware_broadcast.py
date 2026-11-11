#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""A firmware's broadcast standard is what its own etc/starfish/model_id.json says (the region LG's
nyxDeviceInfo module and libdile_tvregion read), from rootfs.pak or a super.pak's logical rootfs,
whose pak names carry no standard."""

import json
import shutil
import subprocess
from pathlib import Path

import pytest
from beanjar import paths, targets

pytestmark = pytest.mark.skipif(
    not (shutil.which("mksquashfs") and shutil.which("unsquashfs")), reason="needs squashfs-tools"
)


def rootfs_image(tmp, identity):
    """a squashfs image whose etc/starfish/model_id.json is identity (a dict; None: no file)"""
    src = Path(tmp) / "rootfs-src"
    shutil.rmtree(src, ignore_errors=True)
    (src / "etc" / "starfish").mkdir(parents=True)
    (src / "etc" / "starfish-release").write_text("Rockhopper release 11.2.0-3711 (queue-qilian)\n")
    if identity is not None:
        (src / "etc" / "starfish" / "model_id.json").write_text(json.dumps(identity))
    img = Path(tmp) / "rootfs.img"
    img.unlink(missing_ok=True)
    subprocess.run(
        ["mksquashfs", str(src), str(img), "-quiet", "-noappend", "-no-progress", "-all-root"],
        check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    return img.read_bytes()


def plain_firmware(tmp, name, identity, paks=()):
    d = Path(tmp) / name
    d.mkdir()
    (d / "rootfs.pak").write_bytes(rootfs_image(tmp, identity))
    for p in paks:
        (d / (p + ".pak")).write_bytes(b"")
    return str(d)


def super_firmware(tmp, name, identity):
    """a measured-layout super.pak firmware (dpmeta.pak's linear map, superpak.py) whose rootfs
    image states identity; its other images are padding"""
    d = Path(tmp) / name
    d.mkdir()
    root = rootfs_image(tmp, identity)
    payloads = {"rootfs": root + bytes(-len(root) % 512)}
    for n in ("fonts", "otncabi", "otycabi", "bsppart"):
        payloads[n] = bytes(512)
    table, offset, data = [], 1024, bytes(1024)
    for index, (n, payload) in enumerate(payloads.items()):
        table += [
            f"{n}-linear,,,ro, 0 {len(payload) // 512} linear /dev/mmcblk0p28 {offset // 512}",
            f"{n},,,ro, 0 1 webos-verity /dev/dm-{index * 2}",
        ]
        data += payload
        offset += len(payload)
    (d / "super.pak").write_bytes(data)
    text = ' dm-mod.waitfor="/dev/mmcblk0p28" dm-mod.create="' + ";".join(table)
    text += '" root=/dev/dm-1 ro rootfstype=squashfs '
    (d / "dpmeta.pak").write_bytes(text.encode() + b"\0")
    return str(d)


def ident(region):
    return {"otaid": "HE_DTV_W25G_AFABJAAA", "machine": "O22N3", "region": region}


@pytest.mark.parametrize(
    "region, broadcast", [("ARIB", "arib"), ("ATSC", "atsc"), ("DVB", "dvb"), ("GLOBAL", None)]
)
def test_model_id_region(tmp_path, region, broadcast):
    fw = plain_firmware(tmp_path, "fw", ident(region))
    assert targets.firmware_broadcast(fw) == broadcast
    assert paths.model_id(fw)["machine"] == "O22N3"


def test_super_pak_firmware_states_its_standard(tmp_path):
    """W25G's, W25O's and W25H's Japanese queue builds: one super.pak, no -arib pak, model_id.json
    ARIB. The board is the chip's Japanese one without vm.toml's firmware.broadcast."""
    fw = super_firmware(tmp_path, "43.21.96.01-HE_DTV_W25G_AFABJAAA", ident("ARIB"))
    assert targets.broadcast_of_paks(targets.firmware_paks(fw)) is None
    assert targets.firmware_broadcast(fw) == "arib"
    for target in (("o22n3", "queue"), ("o24n", "queue"), ("k24n", "queue")):
        assert targets.board_broadcast(target, fw) == "arib"
        assert targets.board_cmdline_src(target, "arib") == (
            f"guest/layers/soc/{target[0]}/rootfs/etc/emulator/board-arib.cmdline"
        )
        assert not targets.boots_default_board(target, fw)
        with pytest.raises(targets.Error, match="the firmware is 'arib'"):
            targets.board_broadcast(target, fw, "atsc")
    glob = super_firmware(tmp_path, "33.31.68.01-HE_DTV_W25G_AFABATAA", ident("GLOBAL"))
    assert targets.firmware_broadcast(glob) is None
    assert targets.board_broadcast(("o22n3", "ponytail"), glob) == "atsc"
    assert targets.board_broadcast(("o22n3", "ponytail"), glob, "dvb") == "dvb"


def test_paks_alone_name_no_standard(tmp_path):
    """Region pak names pick the region's paks (_broadcast_release) but do not state the
    firmware's standard: a firmware before model_id.json (afro to goldilocks2), whose chips have
    one board, has none."""
    old = plain_firmware(tmp_path, "old", None, ["tvservice-dvb", "otncabi-dvb"])
    assert targets.broadcast_of_paks(targets.firmware_paks(old)) == "dvb"
    assert targets.firmware_broadcast(old) is None
    jp = plain_firmware(
        tmp_path, "jp", ident("ARIB"), ["otncabi-arib", "otycabi-arib", "fonts-arib"]
    )
    assert targets.firmware_broadcast(jp) == "arib"
    assert targets.firmware_broadcast(None) is None
    assert targets.firmware_broadcast(str(tmp_path / "missing")) is None


def test_unknown_region_refused(tmp_path):
    fw = plain_firmware(tmp_path, "fw", ident("ISDB"))
    with pytest.raises(targets.Error, match="region 'ISDB'"):
        targets.firmware_broadcast(fw)
    assert targets.firmware_broadcast(plain_firmware(tmp_path, "none", {"otaid": "x"})) is None
