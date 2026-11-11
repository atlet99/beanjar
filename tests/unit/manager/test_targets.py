#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The firmware target table, guest/targets/ (targets.load): every file loads and passes its schema,
and a malformed file is refused with an error that names the file and the key. No VM, no firmware."""

import os
import shutil
import tomllib
from pathlib import Path

import pytest

from beanjar import targets

TOP = Path(__file__).resolve().parents[3]
TARGETS = os.path.join(TOP, "guest", "targets")


def test_every_file_loads():
    """Every TOML file of the tree parses, and the whole table passes the loader's checks:
    each target file is a target, each soc and release file a row, each partmap one a release
    names."""
    files = [os.path.join(d, n) for d, _, ns in os.walk(TARGETS) for n in ns if n.endswith(".toml")]
    assert files
    for f in files:
        with open(f, "rb") as fh:
            tomllib.load(fh)
    table = targets.load(TARGETS)
    names = sorted(n[:-5] for n in os.listdir(TARGETS) if n.endswith(".toml"))
    assert sorted(targets.name(p) for p in table["TARGETS"]) == names
    assert sorted(table["SOCS"]) == sorted(n[:-5] for n in os.listdir(os.path.join(TARGETS, "soc")))
    assert sorted(table["RELEASES"]) == sorted(
        n[:-5] for n in os.listdir(os.path.join(TARGETS, "release"))
    )
    for target in table["TARGETS"]:
        soc = table["TARGET_SOCS"].get(target) or table["SOCS"][target[0]]
        rel = table["TARGET_RELEASES"].get(target) or table["RELEASES"][target[1]]
        assert isinstance(soc, targets.Soc)
        assert isinstance(rel, targets.Release)
        assert not isinstance(rel.partmap, str), targets.name(target)
    assert table["WEBOS_ORDER"][0] == "afro"
    assert table["MODELS"] == targets.MODELS


def test_base_replaces_keys_whole():
    """A row with base is base's with its keys replaced: k8lp's broadcast_params empty, the rest
    of k7lp's kept."""
    k7, k8 = targets.SOCS["k7lp"], targets.SOCS["k8lp"]
    assert k7.broadcast_params
    assert k8.broadcast_params == ()
    assert k8.light_bus == k7.light_bus
    assert targets.RELEASES["beehive"].paks == targets.RELEASES["afro"].paks


class Malformed:
    def __init__(self, tmp):
        self.dir = os.path.join(tmp, "targets")
        shutil.copytree(TARGETS, self.dir)

    def edit(self, rel, old, new):
        path = os.path.join(self.dir, rel)
        with open(path) as f:
            text = f.read()
        assert old in text
        with open(path, "w") as f:
            f.write(text.replace(old, new, 1))
        return path

    def refused(self, *words):
        with pytest.raises(targets.Error) as e:
            targets.load(self.dir)
        for w in words:
            assert w in str(e.value)
        return str(e.value)


@pytest.fixture
def malformed(tmp_path):
    return Malformed(str(tmp_path))


def test_unknown_key(malformed):
    path = malformed.edit(
        "soc/k8hp.toml", 'base = "k8lp"\n', 'base = "k8lp"\nboard_cmdlin = "/x"\n'
    )
    malformed.refused(path, "unknown key 'board_cmdlin'")


def test_wrong_type(malformed):
    path = malformed.edit("release/afro.toml", "cap_last_cap = 36", 'cap_last_cap = "36"')
    malformed.refused(path, "cap_last_cap", "expected an integer")


def test_wrong_type_nested(malformed):
    path = malformed.edit("release/afro.toml", "tool_option_words = 7", "tool_option_words = true")
    malformed.refused(path, "nvm.tool_option_words", "expected an integer")


def test_missing_required_key(malformed):
    path = malformed.edit("release/afro.toml", 'init_system = "upstart"\n', "")
    malformed.refused(path, "missing key 'init_system'")


def test_value_not_allowed(malformed):
    path = malformed.edit("release/afro.toml", 'init_system = "upstart"', 'init_system = "runit"')
    malformed.refused(path, "init_system", "expected one of")


def test_syntax_error(malformed):
    path = malformed.edit("h13-afro.toml", "models = [", "models = [[")
    malformed.refused(path)


def test_unknown_base(malformed):
    path = malformed.edit("soc/k8hp.toml", 'base = "k8lp"', 'base = "k9lp"')
    malformed.refused(path, "base", "k9lp")


def test_unknown_partmap(malformed):
    path = malformed.edit("release/mullet.toml", 'partmap = "mullet"', 'partmap = "mulet"')
    malformed.refused(path, "partmap", "mulet")


def test_target_without_soc(malformed):
    os.rename(
        os.path.join(malformed.dir, "h13-afro.toml"), os.path.join(malformed.dir, "h14-afro.toml")
    )
    malformed.refused("h14-afro.toml", "soc/h14.toml")


def test_bring_up_disagrees(malformed):
    path = malformed.edit("k8hp-queue.toml", 'bring_up = ["W22H"]\n', "")
    malformed.refused(path, "W22H", "bring_up")


def test_target_soc_row(malformed):
    path = malformed.edit("m2-simplesmart.toml", "board_src = ", "board_srcs = ")
    malformed.refused(path, "soc", "unknown key 'board_srcs'")


def test_tested_by_default():
    """A target file without tested is tested: no warning for it."""
    t = targets.TARGETS[("h13", "afro")]
    assert t.tested
    assert targets.tested(("h13", "afro"))
    assert targets.untested_warning(("h13", "afro")) is None


def test_untested_target(malformed):
    """tested = false makes that target untested and no other."""
    malformed.edit(
        "k8hpp-number1.toml", 'models = ["W23H"]\n', 'models = ["W23H"]\ntested = false\n'
    )
    table = targets.load(malformed.dir)
    assert not table["TARGETS"][("k8hpp", "number1")].tested
    for p, t in table["TARGETS"].items():
        if p != ("k8hpp", "number1"):
            assert t.tested == targets.TARGETS[p].tested, targets.name(p)


def test_untested_warning():
    untested = targets.TARGETS[("k8hpp", "number1")]._replace(tested=False)
    with pytest.MonkeyPatch.context() as mp:
        mp.setitem(targets.TARGETS, ("k8hpp", "number1"), untested)
        assert "k8hpp-number1" in targets.untested_targets()
        assert "h13-afro" not in targets.untested_targets()
        assert (
            targets.untested_warning(("k8hpp", "number1"))
            == "k8hpp-number1: not tested yet: may not boot"
        )


def test_tested_true_refused(malformed):
    path = malformed.edit("h13-afro.toml", "models = [", "tested = true\nmodels = [")
    malformed.refused(path, "tested", "only tested = false")


def test_tested_wrong_type(malformed):
    path = malformed.edit("h13-afro.toml", "models = [", 'tested = "no"\nmodels = [')
    malformed.refused(path, "tested", "expected true or false")
