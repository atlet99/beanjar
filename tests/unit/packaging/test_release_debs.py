#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""scripts/release-debs.sh, which the Packages and the Guest workflows both run to fill one release
v<version>, against a fake gh (tests/fakes/fake-gh): the race over who creates the release, the
SHA256SUMS of every .deb whoever writes it last, and Latest only with both halves in."""

import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

TOP = Path(__file__).resolve().parents[3]
SCRIPT = TOP / "scripts/release-debs.sh"
V = "0.5.0+git3.g0123456789ab"
HOST = [f"beanjar_{V}_amd64.deb", f"beanjar_{V}_arm64.deb", f"beanjar-ui_{V}_all.deb"]
GUEST = [f"beanjar-guest_{V}_all.deb", f"beanjar-guest-x86-64_{V}_amd64.deb"]

pytestmark = pytest.mark.skipif(not shutil.which("jq"), reason="the fake gh needs jq")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def unversioned(name):
    package, _, arch = name.removesuffix(".deb").split("_")
    return f"{package}_{arch}.deb"


@pytest.fixture
def run(tmp_path):
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    (bin_dir / "gh").symlink_to(TOP / "tests/fakes/fake-gh")
    state = tmp_path / "release.json"

    def run(tag, names, **env):
        debs = tmp_path / "debs"
        debs.mkdir(exist_ok=True)
        for name in names:
            (debs / name).write_bytes(name.encode())
        e = {
            **os.environ,
            "PATH": f"{bin_dir}:{os.environ['PATH']}",
            "FAKE_GH_STATE": str(state),
            "GH_REPO": "o/r",
            "GITHUB_SHA": "c0ffee",
            "RELEASE_PAUSE": "0",
            **env,
        }
        p = subprocess.run(
            [SCRIPT, tag, *(str(debs / n) for n in names)],
            env=e,
            capture_output=True,
            text=True,
            check=False,
        )
        return p, json.loads(state.read_text()) if state.exists() else None

    return run


def sums_of(names):
    lines = sorted((unversioned(n), sha(n.encode())) for n in names)
    return "".join(f"{digest}  {name}\n" for name, digest in lines)


def test_first_creates_not_latest_and_second_marks_latest(run, tmp_path):
    p, state = run(f"v{V}", GUEST)
    assert p.returncode == 0, p.stderr
    assert state["title"] == f"Beanjar {V}"
    assert "sudo apt-get install" in state["notes"]
    assert state["latest"] is False
    assert sorted(state["assets"]) == sorted(
        ["SHA256SUMS", "beanjar-guest_all.deb", "beanjar-guest-x86-64_amd64.deb"]
    )
    assert state["assets"]["SHA256SUMS"] == sha(sums_of(GUEST).encode())
    p, state = run(f"v{V}", HOST)
    assert p.returncode == 0, p.stderr
    assert state["latest"] is True
    assert state["assets"]["SHA256SUMS"] == sha(sums_of(HOST + GUEST).encode())
    assert state["title"] == f"Beanjar {V}"  # the second run does not create it again


def test_create_lost_to_the_other_falls_through(run):
    p, state = run(f"v{V}", HOST, FAKE_GH_CREATE_RACE="1")
    assert p.returncode == 0, p.stderr
    assert "created meanwhile" in p.stdout
    assert "beanjar_amd64.deb" in state["assets"]


def test_stale_sums_from_the_other_are_rewritten(run, tmp_path):
    # the other workflow's packages appear, and its own SHA256SUMS (without ours) replaces ours,
    # right after our first SHA256SUMS upload
    other = tmp_path / "other.json"
    other.write_text(json.dumps({unversioned(n): sha(n.encode()) for n in GUEST}))
    p, state = run(f"v{V}", HOST, FAKE_GH_OTHER=str(other))
    assert p.returncode == 0, p.stderr
    assert state["assets"]["SHA256SUMS"] == sha(sums_of(HOST + GUEST).encode())
    assert state["latest"] is True


def test_waits_for_digests(run):
    p, state = run(f"v{V}", HOST, FAKE_GH_NO_DIGEST="2")
    assert p.returncode == 0, p.stderr
    assert state["assets"]["SHA256SUMS"] == sha(sums_of(HOST).encode())


def test_tag_must_name_the_files_version(run):
    p, state = run("v0.5.1", HOST)
    assert p.returncode != 0
    assert "the tag v0.5.1 names 0.5.1" in p.stderr
    assert state is None
    p, _ = run(f"guest-{V}", GUEST)
    assert p.returncode != 0
    assert "not a tag v<version>" in p.stderr
