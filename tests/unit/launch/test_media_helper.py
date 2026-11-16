# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The media helper's start: its command line (qemu.plan's hub_argv: the two logs it owns) and its
spawn (launch.start_media_helper: a minimal environment, the pool's socket, stdout and stderr to
RUN_DIR/media-helper.out)."""

import os
import shutil
import socket
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

from beanjar import launch, paths, qemu

from launchkit import fake_tree

# what a desktop session hands its programs, the launcher among them
SESSION = {
    "SSH_AUTH_SOCK": "/run/user/1000/ssh-agent.sock",
    "GPG_AGENT_INFO": "/run/user/1000/gnupg/S.gpg-agent:0:1",
    "DBUS_SESSION_BUS_ADDRESS": "unix:path=/run/user/1000/bus",
    "GITHUB_TOKEN": "ghp_secret",
    "DISPLAY": ":0",
    "WAYLAND_DISPLAY": "wayland-0",
    "XDG_RUNTIME_DIR": "/run/user/1000",
    "PYTHONPATH": "/somewhere",
    "SHELL": "/bin/bash",
    "USER": "someone",
    "NETSTUB_WEB_PASSWORD_FILE": "/home/someone/.netstub-password",
    "MEDIA_HWDEC": "auto",
    # what the helper's libraries read
    "PATH": "/usr/local/bin:/usr/bin:/bin",
    "HOME": "/home/someone",
    "LANG": "C.UTF-8",
    "LC_ALL": "C.UTF-8",
    "LD_LIBRARY_PATH": "/opt/driver/lib",
    "XDG_DATA_DIRS": "/usr/local/share:/usr/share",
    "XDG_CACHE_HOME": "/home/someone/.cache",
    "LIBVA_DRIVER_NAME": "d3d12",
    "GALLIUM_DRIVER": "d3d12",
    "MESA_D3D12_DEFAULT_ADAPTER_NAME": "NVIDIA",
    "VK_ICD_FILENAMES": "/usr/share/vulkan/icd.d/nvidia_icd.json",
    "CUDA_VISIBLE_DEVICES": "0",
}
KEPT = {
    "PATH",
    "HOME",
    "LANG",
    "LC_ALL",
    "LD_LIBRARY_PATH",
    "XDG_DATA_DIRS",
    "XDG_CACHE_HOME",
    "LIBVA_DRIVER_NAME",
    "GALLIUM_DRIVER",
    "MESA_D3D12_DEFAULT_ADAPTER_NAME",
    "VK_ICD_FILENAMES",
    "CUDA_VISIBLE_DEVICES",
}


class EnvironmentTest(unittest.TestCase):
    def test_only_what_the_decoders_read(self):
        env = launch.media_helper_environment(SESSION)
        self.assertEqual(env, {k: SESSION[k] for k in KEPT})

    def test_a_path_without_one(self):
        env = launch.media_helper_environment({"HOME": "/home/someone"})
        self.assertEqual(env, {"HOME": "/home/someone", "PATH": os.defpath})

    def test_not_changed(self):
        env = dict(SESSION)
        launch.media_helper_environment(env)
        self.assertEqual(env, SESSION)


class SpawnTest(unittest.TestCase):
    def setUp(self):
        self.run_dir = tempfile.mkdtemp(prefix="beanjar-launch-")
        self.addCleanup(shutil.rmtree, self.run_dir, ignore_errors=True)

    def plan(self, pool):
        return SimpleNamespace(
            run_dir=self.run_dir,
            env=dict(SESSION),
            pool_socket=os.path.join(self.run_dir, "pool.sock") if pool else None,
            hub_argv=["/b/media-helper", "--pid", "PID", "--log", "L", "--decode-log", "D"],
        )

    def spawn(self, p):
        seen = {}

        def popen(argv, **kw):
            seen["argv"], seen["kw"] = argv, kw
            # the pool's socket is open and listening while the child starts
            if kw["pass_fds"]:
                s = socket.socket(fileno=os.dup(kw["pass_fds"][0]))
                seen["listening"] = s.getsockopt(socket.SOL_SOCKET, socket.SO_ACCEPTCONN)
                s.close()
            seen["out"] = kw["stdout"].name
            return mock.Mock()

        with mock.patch.object(launch.subprocess, "Popen", side_effect=popen):
            launch.start_media_helper(p)
        return seen

    def test_spawn(self):
        p = self.plan(pool=True)
        seen = self.spawn(p)
        argv, kw = seen["argv"], seen["kw"]
        fd = kw["pass_fds"][0]
        self.assertEqual(
            argv,
            [
                "/b/media-helper",
                "--pid",
                str(os.getpid()),
                "--log",
                "L",
                "--decode-log",
                "D",
                "--pool-fd",
                str(fd),
            ],
        )
        self.assertEqual(kw["env"], {k: SESSION[k] for k in KEPT})
        self.assertIs(kw["stdin"], subprocess.DEVNULL)
        self.assertIs(kw["stdout"], kw["stderr"])
        self.assertEqual(seen["out"], os.path.join(self.run_dir, "media-helper.out"))
        self.assertTrue(kw["close_fds"])
        self.assertTrue(seen["listening"])
        self.assertTrue(os.path.exists(p.pool_socket))
        with self.assertRaises(OSError):
            os.fstat(fd)  # the launcher's copy closed once the child has it

    def test_spawn_without_a_pool(self):
        seen = self.spawn(self.plan(pool=False))
        self.assertNotIn("--pool-fd", seen["argv"])
        self.assertEqual(seen["kw"]["pass_fds"], ())


class ArgvTest(unittest.TestCase):
    def test_owned_logs(self):
        """The helper is given its two logs in RUN_DIR: the sessions' and the decoder's."""
        tmp = tempfile.mkdtemp(prefix="beanjar-launch-")
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        top, fw, state = fake_tree(tmp)
        helper = os.path.join(top, "build/media-helper/media-helper")
        os.makedirs(os.path.dirname(helper))
        with open(helper, "w") as f:
            f.write("#!/bin/sh\nexit 1\n")
        os.chmod(helper, 0o755)
        run = os.path.join(top, "build")
        env = {
            "FW": fw,
            "STATE": state,
            "RUN_DIR": run,
            "LOG_DIR": run,
            "IDENTITY_DIR": os.path.join(top, "local"),
            "SLOT": "1",
        }
        with (
            mock.patch.object(qemu, "first_rendernode", return_value=None),
            mock.patch.object(qemu, "is_char", side_effect=lambda p: p == "/dev/dxg"),
            mock.patch.object(qemu, "is_vgem", return_value=False),
            mock.patch.object(qemu, "render_driver", return_value=""),
            mock.patch.object(qemu.platform, "machine", return_value="x86_64"),
            mock.patch.object(qemu, "host_aarch32_el0", return_value=None),
        ):
            p = qemu.plan(env, [], accel="tcg", tree=paths.Tree(top), say=lambda m: None, dry=True)
        argv = p.hub_argv
        self.assertEqual(argv[0], helper)
        self.assertEqual(argv[argv.index("--log") + 1], os.path.join(run, "media-sessions.log"))
        self.assertEqual(
            argv[argv.index("--decode-log") + 1], os.path.join(run, "media-helper.log")
        )


if __name__ == "__main__":
    unittest.main()
