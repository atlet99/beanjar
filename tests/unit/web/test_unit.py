#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-web's systemd user unit (host/web/beanjar_web/unit.py) against a fake systemd: start's
transient unit, enable's unit file (systemd's quoting, WantedBy=default.target, lingering), a
transient unit replaced by enable, stop and status of a persistent unit, disable; --hosts carried
into the command line and shown by status. Nothing here
talks to the real user manager or logind (check-web.py runs start, status and stop for real)."""

import argparse
import contextlib
import io
import os
import subprocess
import sys
from unittest import mock

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../host/web"))
from beanjar_web import unit  # noqa: E402

UNIT = "test-web-unit"


class FakeSystemd:
    """systemctl --user and systemd-run for one unit: whether it runs, from what, enabled."""

    def __init__(self, path):
        self.path = path  # the unit file systemd would load
        self.active = "inactive"
        self.transient = False
        self.loaded = False  # the unit file as of the last daemon-reload
        self.enabled = False
        self.calls = []

    def systemctl(self, *words):
        self.calls.append(("systemctl", *words))
        ok = subprocess.CompletedProcess(words, 0, "", "")
        verb = words[0]
        if verb == "show":
            pid = 4242 if self.active == "active" else 0
            return subprocess.CompletedProcess(
                words, 0, f"ActiveState={self.active}\nMainPID={pid}\n", ""
            )
        if verb == "stop":
            self.active = "inactive"
            self.transient = False  # --collect: the transient unit is gone
        elif verb == "daemon-reload":
            self.loaded = os.path.isfile(self.path)
        elif verb == "enable":
            if self.transient or not self.loaded:
                return subprocess.CompletedProcess(words, 1, "", "Unit is transient or not found")
            self.enabled = True
        elif verb == "start":
            if not self.loaded:
                return subprocess.CompletedProcess(words, 5, "", "Unit not found.")
            self.active = "active"
        elif verb == "is-enabled":
            return subprocess.CompletedProcess(
                words, 0, "enabled\n" if self.enabled else "disabled\n", ""
            )
        elif verb == "disable":
            self.enabled = False
            if "--now" in words:
                self.active = "inactive"
        return ok

    def run(self, argv, **kw):
        self.calls.append(tuple(argv))
        if argv[0] == "systemd-run":
            if self.loaded:
                return subprocess.CompletedProcess(
                    argv, 1, "", "Unit already loaded or has a fragment file"
                )
            self.active, self.transient = "active", True
        return subprocess.CompletedProcess(argv, 0, "", "")


def options(**kw):
    a = dict(listen="127.0.0.1", port=8059, auth="none", token_file=None, password_file=None,
             certificate=None, key=None, size=None, fps=None, hosts=None,
             inspector_port=None, inspector_origin=None,
             novnc="/usr/share/novnc", icons="/usr/share/bootstrap-icons/svg")  # fmt: skip
    a.update(kw)
    return argparse.Namespace(**a)


class TestUnitTest:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path):
        self.tmp = str(tmp_path)
        env = {"XDG_CONFIG_HOME": os.path.join(self.tmp, "config"),
               "XDG_RUNTIME_DIR": os.path.join(self.tmp, "run"), "BEANJAR_WEB_UNIT": UNIT,
               "BEANJAR_LINGER": "0", "PATH": "/usr/bin:/bin"}  # fmt: skip
        self.path = os.path.join(self.tmp, "config", "systemd", "user", f"{UNIT}.service")
        self.sd = FakeSystemd(self.path)
        self.linger = mock.Mock()
        self.state = state = {"pid": 4242, "listen": "127.0.0.1", "port": 8059,
                              "scheme": "http", "auth": "none", "file": None}  # fmt: skip
        started = []
        for patch in (
            mock.patch.dict(os.environ, env),
            mock.patch.object(unit, "systemctl", self.sd.systemctl),
            mock.patch.object(unit, "have_systemd", lambda: True),
            mock.patch.object(unit, "linger", self.linger),
            mock.patch.object(unit.subprocess, "run", self.sd.run),
            mock.patch.object(unit, "read_state",
                              lambda u: state if self.sd.active == "active" else None),
        ):  # fmt: skip
            patch.start()
            started.append(patch)
        yield
        for patch in reversed(started):
            patch.stop()

    def call(self, fn, *args, **kw):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = fn(*args, **kw)
        return rc, out.getvalue(), err.getvalue()

    def test_quote(self):
        q = unit.systemd_quote
        assert q("/a b/c") == '"/a b/c"'
        assert q('x"y\\z') == '"x\\"y\\\\z"'
        assert q("50%") == '"50%%"'
        assert q("$HOME", exec_line=True) == '"$$HOME"'
        assert q("K=$HOME") == '"K=$HOME"', "Environment= does not expand $"
        with pytest.raises(ValueError):
            q("a\nb")

    def test_unit_file(self):
        props, env, argv = unit.service(options(), "/usr/bin/env A=1%x beanjar", UNIT)
        text = unit.unit_file(props, env, argv)
        lines = text.splitlines()
        assert "[Install]" in lines
        assert lines[-1] == "WantedBy=default.target"
        for p in ("Restart=on-failure", "RestartPreventExitStatus=2",
                  f"RuntimeDirectory={UNIT}", "RuntimeDirectoryMode=0700"):  # fmt: skip
            assert p in lines
        exec_line = next(line for line in lines if line.startswith("ExecStart="))
        assert exec_line.startswith(f'ExecStart="{sys.executable}" "-m" "beanjar_web" "serve"')
        assert '"--beanjar" "/usr/bin/env A=1%%x beanjar"' in exec_line
        assert f'"--state-file" "{os.path.join(self.tmp, "run", UNIT, "state.json")}"' in exec_line
        assert 'Environment="PYTHONDONTWRITEBYTECODE=1"' in lines
        assert "BEANJAR_WEB_UNIT" not in text

    def test_hosts(self):
        _props, _env, argv = unit.service(options(), "/bin/true", UNIT)
        assert "--hosts" not in argv
        assert "--hosts" not in "\n".join(unit.describe(self.state))
        _props, _env, argv = unit.service(options(hosts=("rk-2", "local")), "/bin/true", UNIT)
        assert argv[argv.index("--hosts") + 1] == "rk-2,local"
        rc, _out, err = self.call(unit.enable, options(hosts=("rk-2",)), "/bin/true")
        assert rc == 0, err
        with open(self.path) as f:
            assert '"--hosts" "rk-2"' in f.read()
        self.state["hosts"] = ["rk-2"]
        rc, out, _ = self.call(unit.status)
        assert "beanjar-web: only the VMs of --hosts rk-2" in out

    def test_inspector(self):
        """The traffic inspector's own listener and public origin go into the unit's command line;
        status says its port."""
        _props, _env, argv = unit.service(options(), "/bin/true", UNIT)
        assert "--inspector-port" not in argv and "--inspector-origin" not in argv
        _props, _env, argv = unit.service(
            options(inspector_port=8070, inspector_origin="https://i.example"), "/bin/true", UNIT
        )
        assert argv[argv.index("--inspector-port") + 1] == "8070"
        assert argv[argv.index("--inspector-origin") + 1] == "https://i.example"
        self.state["inspector_port"] = 8070
        assert "the traffic inspector on port 8070" in "\n".join(unit.describe(self.state))

    def test_token_file_not_token(self):
        token_file = os.path.join(self.tmp, "web.token")
        with open(token_file, "w") as f:
            f.write("s3cret-token\n")
        props, env, argv = unit.service(
            options(auth="token", token_file=token_file), "/bin/true", UNIT
        )
        text = unit.unit_file(props, env, argv)
        assert f'"--token-file" "{token_file}"' in text
        assert "s3cret-token" not in text

    def test_start_is_transient(self):
        rc, out, _ = self.call(unit.start, options(), "/bin/true")
        assert rc == 0
        assert "started (systemd user unit" in out
        assert self.sd.transient
        assert not os.path.exists(self.path)
        self.linger.assert_called_once()
        rc, out, _ = self.call(unit.status)
        assert "transient: gone at reboot" in out
        rc, out, _ = self.call(unit.stop)
        assert out == f"beanjar-web: stopped (unit {UNIT})\n"

    def test_enable_replaces_transient(self):
        self.call(unit.start, options(), "/bin/true")
        self.linger.reset_mock()
        rc, out, err = self.call(unit.enable, options(), "/bin/true")
        assert rc == 0, err
        assert "stopped the running unit" in out
        assert "enabled, starts at boot" in out
        assert os.path.isfile(self.path)
        assert self.sd.enabled and self.sd.active == "active" and not self.sd.transient
        self.linger.assert_called_once()
        verbs = [c[1] for c in self.sd.calls if c[0] == "systemctl" and c[1] != "show"]
        assert verbs.index("stop") < verbs.index("daemon-reload")
        assert verbs.index("daemon-reload") < verbs.index("enable")
        rc, out, _ = self.call(unit.status)
        assert rc == 0
        assert "persistent, starts at boot" in out

    def test_persistent_stop_start_disable(self):
        self.call(unit.enable, options(), "/bin/true")
        rc, out, _ = self.call(unit.stop)
        assert rc == 0
        assert "for now; still enabled" in out
        assert self.sd.enabled
        rc, out, _ = self.call(unit.status)
        assert rc == 3
        assert "persistent, starts at boot" in out
        # start with options would make a transient unit beside the file: refused
        rc, _out, err = self.call(unit.start, options(), "/bin/true")
        assert rc == 1
        assert "enable OPTIONS" in err
        rc, out, _ = self.call(unit.start, options(), "/bin/true", with_options=False)
        assert (rc, self.sd.active, self.sd.transient) == (0, "active", False)
        rc, out, _ = self.call(unit.disable)
        assert rc == 0
        assert "disabled and stopped" in out
        assert not os.path.exists(self.path)
        assert not (self.sd.enabled or self.sd.active == "active")
        rc, out, _ = self.call(unit.disable)
        assert rc == 0
        assert "not enabled" in out

    def test_enable_again_rewrites(self):
        self.call(unit.enable, options(), "/bin/true")
        rc, _out, err = self.call(unit.enable, options(port=8060), "/bin/true")
        assert rc == 0, err
        with open(self.path) as f:
            assert '"--port" "8060"' in f.read()


if __name__ == "__main__":
    unittest.main(verbosity=2)
