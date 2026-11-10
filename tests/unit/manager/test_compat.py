#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-guest compatibility by what the guest payload has (beanjar/compat.py): the target's components,
every module parameter the host passes declared in the guest .ko's .modinfo (cached), versions as
information only, and a VM with no record of any kind starts."""

import io
import os
import shutil
import struct
import sys
import tomllib
from unittest import mock

import pytest

from beanjar import compat, launch, targets, paths, vmconfig

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))

TARGET = ("h13", "afro")


def elf_with_modinfo(path, entries):
    """A minimal ELF32 relocatable file whose .modinfo holds entries (key=value strings), as a
    kernel module's: only what declared_params reads."""
    modinfo = b"".join(e.encode() + b"\0" for e in entries)
    shstrtab = b"\0.modinfo\0.shstrtab\0"
    data_off = 52
    shstr_off = data_off + len(modinfo)
    sh_off = (shstr_off + len(shstrtab) + 3) & ~3
    header = (
        b"\x7fELF"
        + bytes([1, 1, 1])
        + bytes(9)
        + struct.pack("<HHIIIIIHHHHHH", 1, 40, 1, 0, 0, sh_off, 0, 52, 0, 0, 40, 3, 2)
    )
    null = bytes(40)
    sec_modinfo = struct.pack("<IIIIIIIIII", 1, 1, 2, 0, data_off, len(modinfo), 0, 0, 1, 0)
    sec_shstr = struct.pack("<IIIIIIIIII", 10, 3, 0, 0, shstr_off, len(shstrtab), 0, 0, 1, 0)
    body = header + modinfo + shstrtab
    body += bytes(sh_off - len(body))
    with open(path, "wb") as f:
        f.write(body + null + sec_modinfo + sec_shstr)
    return path


class Env:
    def __init__(self, tmp):
        self.tmp = str(tmp)
        self.repo = os.path.join(self.tmp, "repo")
        self.tree = paths.Tree(self.repo)
        for name in ("webos", "soc", "common"):
            os.makedirs(os.path.join(self.repo, "build", "layers", "h13-afro", name))

    def ko(self, name, *params):
        return elf_with_modinfo(
            os.path.join(self.repo, "build", name + ".ko"),
            [
                "license=GPL",
                *(f"parm={p}:a parameter" for p in params[:1]),
                *(f"parmtype={p}:int" for p in params),
                "vermagic=4.4 SMP",
            ],
        )


@pytest.fixture
def env(tmp_path):
    return Env(tmp_path)


def test_versions_are_information_only(env):
    with open(os.path.join(TOP, "packaging", "VERSION")) as f:
        assert compat.host_version() == f.read().strip()
    assert compat.guest_version(env.tree) == "unknown"
    stamp = os.path.join(env.repo, "build", "guest-installation.toml")
    with open(stamp, "w") as f:
        f.write("version = ")
    assert compat.guest_version(env.tree) == "unknown"
    with open(stamp, "w") as f:
        f.write('version = "9.1.0+git2.g0123456789ab"\n')
    assert compat.run_info(env.tree) == {
        "host_version": compat.host_version(),
        "guest_version": "9.1.0+git2.g0123456789ab",
    }
    compat.components(env.tree, TARGET, "tv")  # another version: no refusal


def test_missing_component_refused(env):
    shutil.rmtree(os.path.join(env.repo, "build", "layers", "h13-afro", "common"))
    with pytest.raises(
        compat.Error,
        match="Cannot start 'tv': the guest payload has no common component for h13-afro",
    ):
        compat.components(env.tree, TARGET, "tv")


def test_declared_params(env):
    ko = env.ko("lgemu", "mic", "cec")
    assert compat.declared_params(ko) == {"mic", "cec"}
    elf_with_modinfo(ko, ["parm=only_parm:described", "parmtype=only_type:bool", "alias=x"])
    assert compat.declared_params(ko) == {"only_parm", "only_type"}


def test_module_params_refusal_names_module_param_and_path(env):
    ko = env.ko("lgemu", "mic", "poll_ms")
    compat.module_params(env.tree, {"lgemu": ("mic=1", "poll_ms=5"), "rtk": ()}, "tv")
    with pytest.raises(compat.Error) as e:
        compat.module_params(env.tree, {"lgemu": ("mic=1", "cec=1")}, "tv")
    assert "lgemu.ko has no parameter cec" in str(e.value)
    assert ko in str(e.value)
    with pytest.raises(compat.Error, match="the guest payload has no .*lg115x.ko"):
        compat.module_params(env.tree, {"lg115x": ("chip_rev=1",)}, "tv")
    with open(os.path.join(env.repo, "build", "rtk.ko"), "w") as f:
        f.write("not an ELF file")
    with pytest.raises(compat.Error, match="cannot read .*rtk.ko's .modinfo"):
        compat.module_params(env.tree, {"rtk": ("fb=1",)}, "tv")


def test_modinfo_cached_by_path_size_mtime(env):
    ko = env.ko("lgemu", "mic")
    real = compat.declared_params
    with mock.patch.object(compat, "declared_params", side_effect=real) as read:
        compat.module_params(env.tree, {"lgemu": ("mic=1",)}, "tv")
        compat.module_params(env.tree, {"lgemu": ("mic=1",)}, "tv")
        assert read.call_count == 1, "read once while the file is the same"
        env.ko("lgemu", "mic", "cec")  # rebuilt: another size
        compat.module_params(env.tree, {"lgemu": ("cec=1",)}, "tv")
        assert read.call_count == 2
        st = os.stat(ko)
        os.utime(ko, ns=(st.st_atime_ns, st.st_mtime_ns + 10**9))  # same size, newer
        compat.module_params(env.tree, {"lgemu": ("cec=1",)}, "tv")
        assert read.call_count == 3
    assert os.path.isfile(env.tree.cache(compat.MODINFO_CACHE))


def test_boot_conf_passes_module_params():
    """boot.conf's MODULE_PARAMS_* are exactly targets.module_params, which the check reads."""
    for target in targets.all_targets():
        derived = {m.name: ("x=1",) for m in targets.soc(target).modules if m.derived}
        try:
            text = targets.boot_conf(target, [], derived)
        except (targets.Error, ValueError):
            continue  # a target whose derived values the stand-in does not satisfy
        emitted = {
            line.split("=", 1)[0][len("MODULE_PARAMS_") :]: line.split("=", 1)[1].strip('"').split()
            for line in text.splitlines()
            if line.startswith("MODULE_PARAMS_")
        }
        want = {
            m.replace("-", "_"): list(w) for m, w in targets.module_params(target, derived).items()
        }
        assert emitted == want, targets.name(target)


@pytest.mark.skipif(
    not os.path.isfile(os.path.join(TOP, "build", "lgemu.ko")), reason="no guest build output"
)
def test_real_guest_modules_declare_what_every_target_passes(env):
    """The published guest modules (build/<module>.ko) declare every static parameter of every target."""
    assert "mic" in compat.declared_params(os.path.join(TOP, "build", "lgemu.ko"))
    tree = paths.Tree(TOP)
    with mock.patch.object(
        tree, "cache", side_effect=lambda *p: os.path.join(env.tmp, "cache", *p)
    ):
        for target in targets.all_targets():
            params = {
                m: w
                for m, w in targets.module_params(target).items()
                if os.path.isfile(tree.build(f"{m}.ko"))
            }
            compat.module_params(tree, params, targets.name(target))


def test_vm_info_beanjar_table(env):
    run = os.path.join(env.tmp, "run")
    os.makedirs(run)
    paths.write_vm_info(
        run, {"platform": "webOS", "version": "1", "beanjar": compat.run_info(env.tree)}
    )
    with open(os.path.join(run, "vm-info.toml"), "rb") as f:
        info = tomllib.load(f)
    assert info["version"] == "1", "version stays the webOS release"
    assert info["beanjar"] == {
        "host_version": compat.host_version(),
        "guest_version": "unknown",
    }


def test_vm_without_any_record_starts(env):
    """No per-VM marker: a VM directory as older Beanjar made it starts, and gets none."""
    lib = os.path.join(env.tmp, "lib")
    vm = os.path.join(lib, "vms", "tv")
    os.makedirs(vm)
    vmconfig.write(os.path.join(vm, "vm.toml"), {"platform": "webOS", "fw": env.tmp, "link": "off"})
    err = io.StringIO()
    with (
        mock.patch.object(launch, "start_check"),
        mock.patch.object(launch, "guest_preflight") as pre,
        mock.patch.object(launch, "bg_start") as bg,
        mock.patch.object(launch, "vm_alive", side_effect=[None, 1]),
        mock.patch.object(sys, "stderr", err),
    ):
        assert launch.main(["start", "--dir", vm]) == 0, err.getvalue()
    pre.assert_called_once()
    bg.assert_called_once()
    assert not os.path.exists(os.path.join(vm, "compatibility.toml"))
