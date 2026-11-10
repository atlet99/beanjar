#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Memory preparation contracts: registry layouts, aliases and rejected malformed inputs."""

import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import sys
from unittest import mock

import pytest

from beanjar import memory, targets

TOP = Path(__file__).resolve().parents[3]

spec = importlib.util.spec_from_file_location(
    "mmap_table", TOP / "host/helpers/mstar/mmap-table.py"
)
table = importlib.util.module_from_spec(spec)
spec.loader.exec_module(table)


def module(target, name="mstar-utopia"):
    return next(m for m in targets.soc(target).modules if m.name == name)


def test_current_targets():
    for target in targets.all_targets():
        derived = {
            m.name: (("mmap_file=/host/mstar-mmap.bin",) if m.derived == "mstar-mmap" else ())
            for m in targets.soc(target).modules
            if m.derived
        }
        targets.boot_conf(target, [], derived)


def test_lm14_requires_derived_transport(tmp_path):
    target = ("lm14", "netcast45")
    # A boot.conf uses the guest path; bytes are checked by derived_files()
    # at host preparation, not by opening a nonexistent /host path here.
    with pytest.raises(targets.Error):
        targets.boot_conf(target, [])
    with pytest.raises(targets.Error):
        targets.boot_conf(target, [], {"mstar-utopia": ()})
    targets.boot_conf(target, [], {"mstar-utopia": ("mmap_file=/host/mstar-mmap.bin",)})
    bad = tmp_path / "mmap.bin"
    bad.write_bytes(b"truncated cache")
    result = mock.Mock(returncode=0, stdout=str(bad), stderr="")
    with mock.patch.object(targets.subprocess, "run", return_value=result):
        with pytest.raises(targets.Error, match="expected 77 records"):
            targets.derived_files(target, "firmware", "target")


def test_aliases_from_registry():
    m = module(("m2", "simplesmart"))
    p = memory.parameters(m.params)
    rows = memory.validate(m.name, m.params)
    for entry in p["mmap_in"].split(","):
        child, parent, offset = map(lambda v: int(v, 0), entry.split(":"))
        assert rows[child][0] == rows[parent][0] + offset
        assert rows[child][2] == rows[parent][2]
    # The registry explicitly places these sibling regions at the same PA.
    assert rows[12][0] == rows[19][0]
    assert rows[20][0] == rows[31][0]


@pytest.mark.parametrize(
    "word",
    [
        "mmap_ids=0",
        "mmap_slot=0",
        "mmap_slot=4097",
        "lx_mem_id=90",
        "gop_fb=90",
        "secure_shm_id=8",
        "mmap_len=90:4096",
        "mmap_len=27:0",
        "mmap_len=27:4097",
        "mmap_len=27:4096,27:8192",
        "mmap_len=27:0x40000000",
        "mmap_in=12:12:0",
        "mmap_in=12:90:0",
        "mmap_in=12:88:1",
        "mmap_in=12:88:0x800000",
        "mmap_in=12:19:0,19:88:0",
        "mmap_in=12:88:0,12:88:4096",
        "miu0_bus=0xf0000000",
        "mmap_file=/host/file",
        "miu_interval=0xf0000000",
    ],
)
def test_mstar_invalid(word):
    m = module(("m2", "simplesmart"))
    with pytest.raises(ValueError):
        memory.validate(m.name, m.params + (word,))


def test_effective_override_actionable():
    target = ("m2", "dreadlocks")
    with pytest.raises(targets.Error, match=r"m2-dreadlocks: mstar-utopia.ko.*mmap_slot"):
        targets.boot_conf(target, [], vm={"mstar-utopia": ("mmap_slot=1",)})
    m = module(target)
    memory.validate(m.name, m.params + ("mmap_slot=1", "mmap_slot=0x800000"))


def test_realtek():
    m = module(("k5lp", "goldilocks2"), "rtk")
    for word in (
        "dcu1_size=0",
        "dcu1_base=0",
        "dcu1_base=4097",
        "dcu1_size=4097",
        "dcu1_base=0xf0000000",
        "dcu2_base=0x20000000",
        "dcu1_base=0x18000000",
    ):
        with pytest.raises(ValueError):
            memory.validate(m.name, m.params + (word,))
    assert len(memory.validate(m.name, m.params + ("dcu2_size=0",))) == 1


def test_derived_file_contract():
    # Kernel file-layout contract allows absent/unaligned firmware rows and
    # overlaps in the one sparse physical store, not disjoint allocations.
    rows = [(0, 0), (0x1001, 3), (0x1001, 3), (0x80000001, 2)]
    data = table.records(rows, 0x80000000, 0x40000000)
    assert len(memory.binary_records(data, len(rows), 0x40000000)) == len(rows)
    for bad in (
        data[:-1],
        data + bytes(20),
        struct.pack("<5I", 1, 0, 0, 1, 4),
        struct.pack("<5I", 0, 1, 2, 1, 4),
        struct.pack("<5I", 0, 0, 0, 1, 2),
        struct.pack("<5I", 0, 0x3FFFFFFF, 0, 2, 4),
    ):
        with pytest.raises(ValueError):
            memory.binary_records(bad, len(rows) if len(bad) != 20 else 1, 0x40000000)


def test_derived_effective_overrides(tmp_path):
    target = ("lm14", "netcast45")
    m = module(target)
    count = memory.number(memory.parameters(m.params), "mmap_ids")
    path = tmp_path / "mmap.bin"
    # Valid structural rows, not firmware or expected retail addresses.
    path.write_bytes(struct.pack("<5I", 0, 0, 0, 8192, 4) * count)
    result = mock.Mock(returncode=0, stdout=str(path), stderr="")
    with mock.patch.object(targets.subprocess, "run", return_value=result):
        targets.derived_files(target, "firmware", "target")
        for word in ("miu_len=4096", "mmap_ids=76", "mmap_file=/host/unprepared"):
            with pytest.raises(targets.Error):
                targets.derived_files(target, "firmware", "target", {"mstar-utopia": (word,)})
    with pytest.raises(targets.Error):
        targets.boot_conf(
            target,
            [],
            {"mstar-utopia": ("mmap_file=/host/mstar-mmap.bin",)},
            vm={"mstar-utopia": ("mmap_file=/host/unprepared",)},
        )


def test_standalone_cli_arguments(tmp_path):
    library = tmp_path / "library"
    library.write_bytes(b"source fixture")
    out = tmp_path / "mmap.bin"
    rows = [(0, 0), (4096, 4096)]
    argv = [
        "mmap-table.py",
        str(library),
        "--output",
        str(out),
        "--miu-interval",
        "0x80000000",
        "--miu-length",
        "0x40000000",
        "--count",
        "2",
    ]
    with (
        mock.patch.object(sys, "argv", argv),
        mock.patch.object(table, "extract", return_value=rows),
    ):
        table.main()
    assert out.read_bytes() == table.records(rows, 0x80000000, 0x40000000)
    assert json.loads(out.with_suffix(".json").read_text()) == {
        "source": "MAdp_SYS_GetMMAP",
        "sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
        "count": len(rows),
        "miu_interval": 0x80000000,
        "miu_length": 0x40000000,
        "output_sha256": hashlib.sha256(out.read_bytes()).hexdigest(),
    }
    with (
        mock.patch.object(sys, "argv", argv[:-1] + ["3"]),
        mock.patch.object(table, "extract", return_value=rows),
    ):
        with pytest.raises(table.Ambiguous, match="configured mmap_ids"):
            table.main()


def test_cache_provenance_and_corruption(tmp_path):
    # Mock extraction only: expectations are structural contracts, no retail
    # firmware constants or bytes. Source is authoritative over cache output.
    repo = tmp_path
    library = b"authoritative source fixture"
    r = mock.Mock(returncode=0, stdout=library)
    with (
        mock.patch.object(table, "REPO", repo),
        mock.patch("subprocess.run", return_value=r),
        mock.patch.object(table, "extract", return_value=[(0, 0), (4096, 4096)]) as extract,
    ):
        out = table.cached("firmware", "target", 0x80000000, 0x40000000, 2)
        original = out.read_bytes()
        table.cached("firmware", "target", 0x80000000, 0x40000000, 2)
        assert extract.call_count == 1
        out.write_bytes(original[:-1])
        table.cached("firmware", "target", 0x80000000, 0x40000000, 2)
        assert extract.call_count == 2
        assert out.read_bytes() == original
        metadata = json.loads(out.with_suffix(".json").read_text())
        # A corrupted but structurally valid cache must not be blessed.
        changed = bytearray(original)
        struct.pack_into("<I", changed, 20 + 12, 8192)
        out.write_bytes(changed)
        table.cached("firmware", "target", 0x80000000, 0x40000000, 2)
        assert extract.call_count == 3
        assert out.read_bytes() == original
        metadata["sha256"] = "stale source"
        out.with_suffix(".json").write_text(json.dumps(metadata))
        table.cached("firmware", "target", 0x80000000, 0x40000000, 2)
        assert extract.call_count == 4
        with pytest.raises(table.Ambiguous, match="configured mmap_ids"):
            table.cached("firmware", "target", 0x80000000, 0x40000000, 3)
