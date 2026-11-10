#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Installed resources stay immutable; generated data belongs to the user."""

from pathlib import Path
import importlib.util
import os
import subprocess
import sys
from unittest import mock

import pytest

TOP = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(TOP / "host/core"))
from beanjar import paths, lls, share


def test_paths_and_prebuilt_probe(tmp_path):
    root = tmp_path
    tree = paths.Tree(root)
    assert tree.cache("lls") == str(root / "build/lls")
    assert tree.mitmweb() == str(root / "build/mitmproxy-venv/bin/mitmweb")
    (root / "installation.toml").write_text('version="test"\n')
    with mock.patch.dict(os.environ, XDG_CACHE_HOME=str(root / "user-cache")):
        assert tree.cache("lls") == str(root / "user-cache/beanjar/lls")
        with mock.patch("beanjar.mitmsetup.executable", return_value="/user/tool/mitmweb"):
            assert tree.mitmweb() == "/user/tool/mitmweb"
        assert tree.build("lls", "lls-probe") == "/usr/share/beanjar/guest/bin/lls-probe"
        with mock.patch("os.path.isfile", return_value=True):
            assert lls.probe(tree) == "/usr/share/beanjar/guest/bin/lls-probe"
            assert lls.probe(tree, preload=True) == "/usr/share/beanjar/guest/bin/lls-probe.so"
    assert tree.build("Image") == "/usr/share/beanjar/guest/Image"
    assert tree.build("vnc", "beanjar-vnc") == "/usr/bin/beanjar-vnc"
    assert (
        tree.src("guest/layers/soc/lm21a/rootfs/etc/emulator/board.cmdline")
        == "/usr/share/beanjar/guest/boards/lm21a/board.cmdline"
    )
    assert (
        tree.src("guest/layers/soc/o22/rootfs/etc/emulator/board-arib.cmdline")
        == "/usr/share/beanjar/guest/boards/o22/board-arib.cmdline"
    )
    # the target table, the beanjar package's (packaging/debian/build.py's stage_host)
    assert (
        tree.src("guest", "targets", "soc", "k8hp.toml")
        == "/usr/share/beanjar/targets/soc/k8hp.toml"
    )
    assert tree.src("host/helpers/network/tlsca.py") == str(root / "helpers/network/tlsca.py")
    assert tree.pkg_parent == str(root / "core")


def test_share_uses_guest_data_and_refuses_escaping_links(tmp_path):
    root = tmp_path
    library = root / "lib"
    library.mkdir()
    (library / "installation.toml").write_text('version="test"')
    tree = paths.Tree(library)
    guest = root / "share/guest"
    (guest / "scripts").mkdir(parents=True)
    file = guest / "scripts/save-logs.sh"
    file.write_text("runtime script")

    def source(path):
        return str(guest if path == "guest" else guest / path.removeprefix("guest/"))

    with mock.patch.object(tree, "src", side_effect=source):
        assert share._checkout_rel(tree, "guest/scripts/save-logs.sh") == (
            "guest/scripts/save-logs.sh",
            str(file),
        )
        file.unlink()
        file.symlink_to(root / "private")
        with pytest.raises(share.Error, match="not a packaged"):
            share._checkout_rel(tree, "guest/scripts/save-logs.sh")


def test_package_depends_on_vendored_or_system_qemu():
    spec = importlib.util.spec_from_file_location("debian_build", TOP / "packaging/debian/build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    groups = [
        group.strip()
        for group in build.host_dependencies("0.2.1", "libc6", build.load_identity()).split(",")
    ]
    assert groups[:2] == ["beanjar-guest (>= 0.2)", "beanjar-guest (<< 0.3)"], (
        "any guest of the host's contract, never one pinned to the host version"
    )
    assert "beanjar-qemu | qemu-system-arm" in groups
    assert "beanjar-qemu | qemu-system-modules-opengl" in groups
    assert "qemu-system-arm" not in groups, "the system QEMU only as the alternative"
    assert not (TOP / "packaging/debian/build-deps/system-qemu.control").exists()


def test_vnc_package_depends_on_the_same_beanjar():
    spec = importlib.util.spec_from_file_location("debian_build", TOP / "packaging/debian/build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    groups = [group.strip() for group in build.vnc_dependencies("1.2", "libneatvnc1").split(",")]
    assert groups == ["beanjar (= 1.2)", "libneatvnc1"]


def test_web_package(tmp_path):
    """beanjar-web: the same beanjar, beanjar-vnc, aiohttp and Debian's noVNC; its tree holds
    the package and a launcher that imports it from /usr/lib/beanjar/web."""
    spec = importlib.util.spec_from_file_location("debian_build", TOP / "packaging/debian/build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    groups = [group.strip() for group in build.web_dependencies("1.2").split(",")]
    assert groups[:2] == ["beanjar (= 1.2)", "beanjar-vnc"]
    assert "python3-aiohttp" in groups
    assert "novnc" in groups
    assert "bootstrap-icons" in groups
    work = str(tmp_path)
    web = build.stage_web(Path(work))
    package = web / "usr/lib/beanjar/web/beanjar_web"
    assert (package / "server.py").is_file()
    assert (package / "static/remote.js").is_file()
    launcher = (web / "usr/bin/beanjar-web").read_text()
    assert "/usr/lib/beanjar/web" in launcher
    assert launcher.startswith(f"#!/usr/bin/python3\n{build.PYCACHE}")


def test_ui_package(tmp_path):
    """beanjar-ui: the same beanjar and GTK for Python, which beanjar itself does not need; its
    tree holds beanjar_ui, the beanjar-ui launcher (the core's package on its path too), both
    bundles and the desktop entry."""
    spec = importlib.util.spec_from_file_location("debian_build", TOP / "packaging/debian/build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    groups = [group.strip() for group in build.ui_dependencies("1.2").split(",")]
    assert groups[0] == "beanjar (= 1.2)"
    assert "gir1.2-gtk-4.0 (>= 4.12)" in groups
    host = build.host_dependencies("1.2.0", "libc6", build.load_identity())
    assert "gtk" not in host
    assert "adw" not in host
    work = str(tmp_path)
    publish = Path(work) / "publish"
    for bundle in ("manager/beanjar.gresource", "viewer/beanjar-viewer.gresource"):
        (publish / bundle).parent.mkdir(parents=True, exist_ok=True)
        (publish / bundle).write_bytes(b"")
    ui = build.stage_ui(Path(work), publish)
    package = ui / "usr/lib/beanjar/ui/beanjar_ui"
    assert (package / "manager/gui.py").is_file()
    assert (package / "viewer/main.py").is_file()
    launcher = (ui / "usr/bin/beanjar-ui").read_text()
    assert launcher.startswith(f"#!/usr/bin/python3\n{build.PYCACHE}")
    assert 'sys.path.insert(0,"/usr/lib/beanjar/ui")' in launcher
    assert 'sys.path.insert(1,"/usr/lib/beanjar/core")' in launcher
    assert "from beanjar_ui.manager.gui import main" in launcher
    assert (ui / "usr/share/beanjar/ui/beanjar.gresource").is_file()
    desktop = (ui / "usr/share/applications/org.webosbrew.beanjar.desktop").read_text()
    assert "Exec=beanjar-ui\n" in desktop


def test_launcher_bytecode_cache(tmp_path):
    """The launchers keep bytecode in $XDG_CACHE_HOME/beanjar/pycache (default ~/.cache),
    owner-only, never beside the installed sources; without a directory of this user's own
    there, none is written."""
    spec = importlib.util.spec_from_file_location("debian_build", TOP / "packaging/debian/build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    code = build.PYCACHE + (
        "sys.path.insert(0, sys.argv[1])\nimport probe_mod\n"
        "print(sys.pycache_prefix, sys.dont_write_bytecode)\n"
    )
    root = tmp_path
    lib = root / "lib"
    lib.mkdir()
    (lib / "probe_mod.py").write_text("X = 1\n")

    def run(**env):
        environ = {k: v for k, v in os.environ.items() if k != "XDG_CACHE_HOME"}
        result = subprocess.run(
            [sys.executable, "-I", "-c", code, str(lib)],
            env={**environ, **env},
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.split()

    cache = root / "cache/beanjar/pycache"
    assert run(XDG_CACHE_HOME=str(root / "cache")) == [str(cache), "False"]
    assert cache.stat().st_mode & 0o777 == 0o700
    assert list(cache.rglob("probe_mod.*.pyc")), "bytecode in the cache"
    assert not (lib / "__pycache__").exists(), "none beside the sources"
    home = root / "home"
    home.mkdir()
    got = run(XDG_CACHE_HOME="relative", HOME=str(home))
    assert got == [str(home / ".cache/beanjar/pycache"), "False"]
    (root / "file").write_text("")
    assert run(XDG_CACHE_HOME=str(root / "file")) == ["None", "True"]
    if os.geteuid() != 0:  # the nearest existing directory, /, is root's
        assert run(XDG_CACHE_HOME="/") == ["None", "True"]
    assert not (lib / "__pycache__").exists()


def test_shlibs_file_takes_dpkg_shlibdeps_output():
    spec = importlib.util.spec_from_file_location("debian_build", TOP / "packaging/debian/build.py")
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    libs = "libc6 (>= 2.38), libgtk-4-1 (>= 4.12)"
    for text in (
        f"shlibs:Depends={libs}\n",
        f"shlibs:Depends={libs}\nmisc:Depends=\n",
        f"{libs}\n",
    ):
        assert build.shlibs_depends(text) == libs
    for text in ("", "misc:Depends=foo\n", "shlibs:Depends=\n", "a\nb\n"):
        with pytest.raises(SystemExit, match="shlibs:Depends="):
            build.shlibs_depends(text)
