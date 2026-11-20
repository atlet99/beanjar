# SPDX-FileCopyrightText: The Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path
import runpy
import subprocess
import sys

from beanjar import paths


class BundleTree(paths.Tree):
    def cache(self, *parts):
        return os.path.expanduser(os.path.join("~", "Library", "Caches", "Beanjar", *parts))

    def build(self, *parts):
        path = super().build(*parts)
        if (
            not parts
            or parts[0]
            not in {
                "Image",
                "initramfs.cpio.gz",
                "components",
                "layers",
                "qemu",
                "qemu-user",
                "ssh",
                "guest-installation.toml",
                "lls",
                "testguest",
            }
            and not (len(parts) == 1 and parts[0].endswith(".ko"))
        ):
            return self.cache(*parts)
        return path


root = Path(sys.executable).resolve().parents[1] / "Resources"
if len(sys.argv) == 1 and not sys.stdin.isatty():
    raise SystemExit(
        subprocess.call(["/usr/bin/open", "-a", "Terminal", root / "Open Test Guest.command"])
    )
paths.TREE = BundleTree(root)
tools = root / "build/qemu/bin"
os.environ["PATH"] = os.pathsep.join((str(tools), "/usr/bin", "/bin", "/usr/sbin", "/sbin"))
os.environ.setdefault("QEMU", str(tools / "qemu-system-aarch64"))

helpers = root / "host/helpers"
scripts = [
    str(helpers / name)
    for name in ("ssh/dropbear-hostkey.py", "rtk/rpc-table.py", "mstar/mmap-table.py")
]
if sys.argv[1:2] and sys.argv[1] in scripts:
    sys.argv.pop(0)
    runpy.run_path(sys.argv[0], run_name="__main__")
    raise SystemExit(0)

from beanjar import targets

targets.RPC_TABLE, targets.MMAP_TABLE = scripts[1:]

if sys.argv[1:2] == ["vm-launcher"] or sys.argv[1:3] == ["-m", "beanjar.launch"]:
    from beanjar import launch

    raise SystemExit(launch.main(sys.argv[2:] if sys.argv[1] == "vm-launcher" else sys.argv[3:]))

from beanjar import cli

raise SystemExit(cli.main())
