#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""MITM consent and failed setup cannot start or replace a working installation."""

import subprocess
import sys
from unittest import mock

import pytest

from beanjar import mitmsetup, paths

TREE = paths.TREE


@pytest.fixture
def base(tmp_path):
    base = tmp_path / "tool"
    with mock.patch.object(mitmsetup, "root", return_value=base):
        yield base


@pytest.fixture
def spawn_user():
    users = []

    def spawn(script):
        user = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)", str(script)])
        users.append(user)
        return user

    yield spawn
    for user in users:
        user.kill()
        user.wait()


def test_noninteractive_and_decline_do_not_install(base):
    with (
        mock.patch.object(sys.stdin, "isatty", return_value=False),
        mock.patch.object(mitmsetup, "install") as install,
    ):
        with pytest.raises(mitmsetup.Error, match="beanjar mitm setup --install"):
            mitmsetup.ensure(TREE)
        install.assert_not_called()
    with (
        mock.patch.object(sys.stdin, "isatty", return_value=True),
        mock.patch("builtins.input", return_value="no"),
        mock.patch.object(mitmsetup, "install") as install,
    ):
        with pytest.raises(mitmsetup.Error, match="declined"):
            mitmsetup.ensure(TREE)
        install.assert_not_called()


def test_failure_keeps_old_current_and_removes_incomplete_environment(base):
    old = base / "environments/old"
    old.mkdir(parents=True)
    (old / "keep").write_text("working")
    (base / "current").symlink_to("environments/old")
    with mock.patch.object(mitmsetup, "_run", side_effect=subprocess.CalledProcessError(1, "pip")):
        with pytest.raises(mitmsetup.Error, match="setup failed"):
            mitmsetup.install(TREE)
    assert (base / "current").resolve() == old
    assert (old / "keep").read_text() == "working"
    assert not mitmsetup.environment(TREE).exists()
    assert (base / "setup.log").is_file()


def test_unvalidated_executable_is_not_available(base):
    env = mitmsetup.environment(TREE)
    (env / "bin").mkdir(parents=True)
    (env / "bin/mitmweb").write_text("#!/bin/sh\n")
    (env / "bin/mitmweb").chmod(0o755)
    (base / "current").symlink_to(env.relative_to(base))
    assert not mitmsetup.available(TREE)
    (env / ".validated").write_text(mitmsetup.fingerprint(TREE))
    assert mitmsetup.available(TREE)


def test_install_prunes_unused_environments(base, spawn_user):
    """After a successful install the other environments go, except one a running mitmweb uses."""
    envs = base / "environments"
    for name in ("older", "running"):
        (envs / name / "bin").mkdir(parents=True)
    (base / "current").symlink_to("environments/running")
    # a stand-in for mitmweb: the venv's script as an argument, as its shebang runs it
    user = spawn_user(envs / "running/bin/mitmweb")
    made = lambda *_a, **_k: mitmsetup.environment(TREE).mkdir(
        parents=True, exist_ok=True
    )  # venv, pip
    with (
        mock.patch.object(mitmsetup, "_run", side_effect=made),
        mock.patch.object(mitmsetup, "validate"),
    ):
        directory = mitmsetup.install(TREE)
    assert (base / "current").resolve() == directory
    assert sorted(p.name for p in envs.iterdir()) == sorted(["running", directory.name])
    user.kill()
    user.wait()
    assert mitmsetup.prune(directory) == [envs / "running"]
    assert [p.name for p in envs.iterdir()] == [directory.name]


def test_setup_prunes_when_already_installed(base, spawn_user):
    """mitm setup prunes on every successful run, also when there is nothing to install."""
    envs = base / "environments"
    env = mitmsetup.environment(TREE)
    (env / "bin").mkdir(parents=True)
    (env / "bin/mitmweb").write_text("#!/bin/sh\n")
    (env / "bin/mitmweb").chmod(0o755)
    (env / ".validated").write_text(mitmsetup.fingerprint(TREE))
    (base / "current").symlink_to(env.relative_to(base))
    for name in ("older", "running"):
        (envs / name / "bin").mkdir(parents=True)
    spawn_user(envs / "running/bin/mitmweb")
    with (
        mock.patch.object(mitmsetup, "validate"),
        mock.patch.object(mitmsetup, "install") as install,
        mock.patch("sys.stdout"),
    ):
        assert mitmsetup.main(["--install"]) == 0
        install.assert_not_called()
    assert sorted(p.name for p in envs.iterdir()) == sorted(["running", env.name])


def test_declined_setup_prunes_nothing(base):
    old = base / "environments/old"
    old.mkdir(parents=True)
    with mock.patch.object(sys.stdin, "isatty", return_value=False), mock.patch("sys.stderr"):
        assert mitmsetup.main([]) == 1
    assert old.is_dir()


def test_failed_install_prunes_nothing(base):
    old = base / "environments/old"
    old.mkdir(parents=True)
    with mock.patch.object(mitmsetup, "_run", side_effect=subprocess.CalledProcessError(1, "pip")):
        with pytest.raises(mitmsetup.Error):
            mitmsetup.install(TREE)
    assert old.is_dir()
