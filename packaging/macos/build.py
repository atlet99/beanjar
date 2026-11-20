# SPDX-FileCopyrightText: The Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build and sign the Apple Silicon CLI preview ad hoc or with a Keychain identity."""

import argparse
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import tempfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def brew(name):
    return Path(output("brew", "--prefix", name))


def copy(src, dest, *ignore):
    dest.parent.mkdir(parents=True, exist_ok=True)
    if src.is_dir():
        shutil.copytree(
            src,
            dest,
            symlinks=True,
            ignore=shutil.ignore_patterns(".DS_Store", "__pycache__", *ignore),
        )
    else:
        shutil.copy2(src, dest)
        dest.chmod(dest.stat().st_mode | 0o200)


def bundle_tools(dest):
    qemu, squashfs, e2fs, openssl = map(brew, ("qemu", "squashfs", "e2fsprogs", "openssl@3"))
    lib = dest / "lib"
    seen = {}

    def add(src, target):
        copy(src, target)
        for line in output("otool", "-L", src).splitlines()[1:]:
            name = line.strip().split(" (", 1)[0]
            if name.startswith(("/usr/lib/", "/System/Library/")) or name == str(src):
                continue
            dependency = Path(name)
            if not dependency.is_absolute():
                raise RuntimeError(f"Unsupported load path in {src}: {name}")
            dylib = lib / dependency.name
            real = dependency.resolve(strict=True)
            if dylib in seen and seen[dylib] != real:
                raise RuntimeError(f"Conflicting libraries: {seen[dylib]} and {real}")
            if dylib not in seen:
                seen[dylib] = real
                add(dependency, dylib)
            if dependency.resolve() == src.resolve():
                run("install_name_tool", "-id", f"@rpath/{dylib.name}", target)
            else:
                rel = os.path.relpath(dylib, target.parent)
                run("install_name_tool", "-change", name, f"@loader_path/{rel}", target)

    for prefix, paths in (
        (qemu, ("bin/qemu-system-aarch64", "bin/qemu-img")),
        (squashfs, ("bin/mksquashfs", "bin/unsquashfs")),
        (e2fs, ("sbin/mke2fs",)),
        (openssl, ("bin/openssl",)),
    ):
        for path in paths:
            add(prefix / path, dest / "bin" / Path(path).name)
    copy(qemu / "share/qemu", dest / "share/qemu")


def app_icon(root, work, resources):
    iconset = work / "AppIcon.iconset"
    iconset.mkdir()
    png = work / "icon.png"
    run("magick", "-background", "none", "-density", "192", root / "host/ui/data/icon.svg", png)
    for size in (16, 32, 128, 256, 512):
        for scale, suffix in ((1, ""), (2, "@2x")):
            pixels = str(size * scale)
            run(
                "sips",
                "-z",
                pixels,
                pixels,
                png,
                "--out",
                iconset / f"icon_{size}x{size}{suffix}.png",
                stdout=subprocess.DEVNULL,
            )
    run("iconutil", "-c", "icns", iconset, "-o", resources / "AppIcon.icns")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--identity",
        default="-",
        help="exact Keychain identity name or SHA-1 hash (default: ad hoc)",
    )
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("requires Apple Silicon macOS")
    root = Path(__file__).resolve().parents[2]
    os.chdir(root)
    build = root / "build"
    version = (root / "packaging/VERSION").read_text().strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        parser.error("packaging/VERSION must contain a numeric macOS bundle version")
    identity = args.identity
    ad_hoc = identity == "-"
    suffix = "-adhoc" if ad_hoc else ""
    out = build / f"Beanjar-{version}-macos-arm64-preview{suffix}.dmg"
    if not ad_hoc:
        identities = output("security", "find-identity", "-v", "-p", "codesigning")
        matches = {
            fingerprint
            for fingerprint, name in re.findall(
                r'^\s*\d+\) ([0-9A-Fa-f]{40}) "(.*)"$', identities, re.M
            )
            if name == identity or fingerprint.upper() == identity.upper()
        }
        if not matches:
            parser.error("signing identity is not valid or is missing from Keychain")
        if len(matches) != 1:
            parser.error("more than one signing identity has that name; pass its SHA-1 hash")
        identity = matches.pop()

    def sign(path, *options):
        run("codesign", "--force", "--sign", identity, "--timestamp=none", *options, path)
        run("codesign", "--verify", "--strict", path)

    with tempfile.TemporaryDirectory(prefix="macos-dmg-", dir=build) as scratch:
        work = Path(scratch)
        volume = work / "volume"
        run(
            "uv",
            "run",
            "--no-project",
            "--python",
            "3.14",
            "--with",
            "pyinstaller==6.22.3",
            "--with",
            "capstone",
            "--with",
            "pyelftools",
            "pyinstaller",
            "--onedir",
            "--windowed",
            "--target-architecture",
            "arm64",
            "--name",
            "Beanjar",
            "--paths",
            "host/core",
            "--collect-submodules",
            "beanjar",
            "--collect-all",
            "capstone",
            "--collect-all",
            "elftools",
            "--distpath",
            work / "dist",
            "--workpath",
            work / "pyi",
            "--specpath",
            work,
            "packaging/macos/entry.py",
        )
        volume.mkdir()
        app = volume / "Beanjar.app"
        (work / "dist/Beanjar.app").rename(app)
        contents = app / "Contents"
        macos = contents / "MacOS"
        resources = contents / "Resources"
        frameworks = contents / "Frameworks"
        (contents / "Info.plist").write_bytes(
            plistlib.dumps(
                {
                    "CFBundleIdentifier": "org.webosbrew.beanjar",
                    "CFBundleName": "Beanjar",
                    "CFBundleDisplayName": "Beanjar",
                    "CFBundleExecutable": "Beanjar",
                    "CFBundlePackageType": "APPL",
                    "CFBundleIconFile": "AppIcon.icns",
                    "CFBundleShortVersionString": version,
                    "CFBundleVersion": version,
                    "LSMinimumSystemVersion": "26.0",
                    "LSApplicationCategoryType": "public.app-category.developer-tools",
                    "NSHumanReadableCopyright": "Copyright Beanjar contributors",
                }
            )
        )
        app_icon(root, work, resources)
        demo = resources / "Open Test Guest.command"
        copy(root / "packaging/macos/demo.sh", demo)
        demo.chmod(0o755)
        for path in (
            "guest/targets",
            "guest/scripts",
            "guest/layers/headless/rootfs",
            "guest/testguest/rootfs/etc/board.cmdline",
            "guest/helpers/lls/lls-probe.c",
            "host/helpers/ssh/dropbear-hostkey.py",
            "host/helpers/network/tlsca.py",
            "host/helpers/rtk",
            "host/helpers/mstar",
            "LICENSE",
            "LICENSES",
        ):
            copy(root / path, resources / path)
        for path in (
            "Image",
            "initramfs.cpio.gz",
            "guest-installation.toml",
            "qemu-user/bin/qemu-arm",
            "testguest/image",
            "layers/virt-testguest",
            "ssh",
            "lls/lls-probe",
            "lls/lls-probe.so",
            *(p.name for p in build.glob("*.ko")),
        ):
            copy(build / path, resources / "build" / path)
        copy(build / "components", resources / "build/components", "gl")
        # The packaged test guest uses a local layer so the demo works with published components too.
        layer = resources / "build/layers/virt-testguest/testguest"
        from sys import path as python_path

        python_path.insert(0, str(root / "host/core"))
        from beanjar import guestlayers

        component = guestlayers.component(("virt", "testguest"), "testguest")
        selected = guestlayers.select(str(build / "components"), ("virt", "testguest"), component)
        if selected:
            shutil.copytree(selected, layer, symlinks=True, dirs_exist_ok=True)
            shutil.rmtree(resources / "build/components" / component)
        boot_frame = layer / "usr/share/beanjar/boot-frame.bgra"
        boot_frame.parent.mkdir(parents=True, exist_ok=True)
        run(
            "magick",
            "-background",
            "none",
            root / "host/ui/data/icon.svg",
            "-resize",
            "168x168",
            "-background",
            "#10282d",
            "-gravity",
            "center",
            "-extent",
            "800x600",
            "-font",
            "/System/Library/Fonts/Helvetica.ttc",
            "-fill",
            "white",
            "-pointsize",
            "30",
            "-annotate",
            "+0+124",
            "Beanjar Test Guest",
            "-pointsize",
            "18",
            "-annotate",
            "+0+163",
            "Running on Apple Silicon  /  HVF",
            "-alpha",
            "off",
            "-depth",
            "8",
            f"BGRA:{boot_frame}",
        )
        demo_init = layer / "etc/init.d/S98beanjar-display"
        copy(root / "packaging/macos/guest-display.sh", demo_init)
        demo_init.chmod(0o755)
        qemu = resources / "build/qemu"
        bundle_tools(qemu)
        entitlements = work / "qemu.plist"
        entitlements.write_bytes(plistlib.dumps({"com.apple.security.hypervisor": True}))
        run("xattr", "-crs", app)
        binaries = [
            *frameworks.rglob("*"),
            *(qemu / "lib").iterdir(),
            *(qemu / "bin").iterdir(),
        ]
        for path in binaries:
            if path.is_symlink() or not path.is_file():
                continue
            if b"Mach-O" in subprocess.check_output(("file", "-b", path)):
                options = (
                    ("--entitlements", entitlements)
                    if path == qemu / "bin/qemu-system-aarch64"
                    else ()
                )
                sign(path, *options)
        for path in sorted(frameworks.rglob("*.framework"), key=lambda p: -len(p.parts)):
            if path.is_dir() and not path.is_symlink():
                sign(path)
        sign(app)
        run("codesign", "--verify", "--deep", "--strict", app)
        run(macos / "Beanjar", "--help", stdout=subprocess.DEVNULL)
        run(qemu / "bin/qemu-system-aarch64", "--version")
        (volume / "Applications").symlink_to("/Applications")
        copy(root / "packaging/macos/README.txt", volume / "README.txt")
        image = work / out.name
        run(
            "hdiutil",
            "create",
            "-srcfolder",
            volume,
            "-volname",
            "Beanjar",
            "-fs",
            "APFS",
            "-format",
            "UDZO",
            image,
        )
        if not ad_hoc:
            sign(image)
        run("hdiutil", "verify", image)
        image.replace(out)
    print(out)


if __name__ == "__main__":
    main()
