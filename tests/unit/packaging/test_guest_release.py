#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The released beanjar-guest the Packages workflow pairs with its host (packaging/guest_release.py):
which release of a list it picks, and the checks of what it downloads."""

import hashlib
import importlib.util
import itertools
import json
import subprocess
from pathlib import Path
from unittest import mock

import pytest

TOP = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("guest_release", TOP / "packaging/guest_release.py")
guest_release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest_release)

H = "0123456789ab"


GUEST = ["beanjar-guest_all.deb", "beanjar-guest-x86-64_amd64.deb", "SHA256SUMS"]


def release(tag, draft=False, assets=GUEST):
    return {
        "tag_name": tag,
        "draft": draft,
        "assets": [{"name": n, "url": f"api:{n}"} for n in assets],
    }


RELEASES = [
    release("v0.3.9"),
    release("v0.4.0"),
    release(f"v0.4.0+git12.g{H}"),
    release(f"v0.4.0+git3.g{H}"),
    release("v0.4.2", draft=True),
    # the host's half only: the Guest workflow has not finished
    release("v0.4.3", assets=["beanjar_amd64.deb", "SHA256SUMS"]),
    release("v0.5.0"),
    release("v1.0.0"),
    release("guest-0.4.9"),
    release("host-0.4.9"),
    release("v0.4.x"),
]


def test_newest_of_the_contract():
    version, picked = guest_release.select(RELEASES, "0.4")
    assert version == f"0.4.0+git12.g{H}"
    assert picked["tag_name"] == f"v{version}"


def test_patch_beats_commits_and_drafts_skipped():
    version, _ = guest_release.select(RELEASES + [release("v0.4.1")], "0.4")
    assert version == "0.4.1"


def test_other_contracts():
    assert guest_release.select(RELEASES, "0.5")[0] == "0.5.0"
    assert guest_release.select(RELEASES, "1")[0] == "1.0.0"
    assert guest_release.select(RELEASES, "0.6") is None
    assert guest_release.select([], "0.4") is None


def test_order_agrees_with_dpkg():
    versions = ["0.4.0", f"0.4.0+git3.g{H}", f"0.4.0+git12.g{H}", "0.4.1", "0.10.0", "1.0.0"]
    assert sorted(versions[3:] + versions[:3], key=guest_release.order) == versions
    for a, b in itertools.pairwise(versions):
        assert (
            subprocess.run(["dpkg", "--compare-versions", a, "lt", b], check=False).returncode == 0
        )


def test_sums():
    digest = "a" * 64
    text = f"{digest}  beanjar-guest_0.4.0_all.deb\n{digest} *other.deb\nnot a line\n"
    assert guest_release.parse_sums(text) == {
        "beanjar-guest_0.4.0_all.deb": digest,
        "other.deb": digest,
    }


def fake_api(releases, files):
    def request(url, accept):
        if url.startswith(guest_release.API):
            return json.dumps(releases if "page=1" in url else []).encode()
        return files[url.removeprefix("api:")]

    return mock.patch.object(guest_release, "_request", request)


def test_fetch_checks_the_sum(tmp_path):
    deb = b"a guest"
    x86 = b"an x86-64 kernel"
    sums = (
        f"{hashlib.sha256(deb).hexdigest()}  beanjar-guest_all.deb\n"
        f"{hashlib.sha256(x86).hexdigest()}  beanjar-guest-x86-64_amd64.deb\n"
        f"{hashlib.sha256(b'host').hexdigest()}  beanjar_amd64.deb\n"
    ).encode()
    releases = [release("v0.4.0", assets=[*GUEST, "beanjar_amd64.deb"])]
    files = {
        "beanjar-guest_all.deb": deb,
        "beanjar-guest-x86-64_amd64.deb": x86,
        "SHA256SUMS": sums,
    }
    name = "beanjar-guest_0.4.0_all.deb"
    with fake_api(releases, files):
        assert guest_release.fetch("o/r", "0.4.1", tmp_path) == tmp_path / name
    assert (tmp_path / name).read_bytes() == deb
    assert (tmp_path / "beanjar-guest-x86-64_0.4.0_amd64.deb").read_bytes() == x86
    tampered = fake_api(releases, {**files, "beanjar-guest-x86-64_amd64.deb": b"tampered"})
    with tampered, pytest.raises(guest_release.Error, match="SHA-256"):
        guest_release.fetch("o/r", "0.4.1", tmp_path / "bad")
    assert not (tmp_path / "bad").exists()


def test_fetch_none_names_the_tag(tmp_path):
    with (
        fake_api([release("v0.3.0")], {}),
        pytest.raises(
            guest_release.Error,
            match=r"contract 0\.4 .*>= 0\.4, << 0\.5.*tag v<version> on o/r",
        ),
    ):
        guest_release.fetch("o/r", "0.4.0", tmp_path)


def test_main_fetches_for_the_tree(tmp_path, capsys):
    (tmp_path / "packaging").mkdir()
    (tmp_path / "packaging/VERSION").write_text("0.6.0\n")
    with fake_api(RELEASES, {}):
        assert (
            guest_release.main(
                ["fetch", "--out", str(tmp_path / "o"), "--repo", "o/r", "--top", str(tmp_path)]
            )
            == 1
        )
    assert "contract 0.6" in capsys.readouterr().err
