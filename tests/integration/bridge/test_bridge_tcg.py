# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The qemu-user compat bridge on the guest kernel, under full TCG on any host
(docs/dev/architecture/qemu-user-bridge.md): the checkout's build/Image boots in qemu-system-aarch64
with a small initramfs (the published initramfs's busybox, qemu-arm and bridgetest.c, the 32-bit
ABI check, built static), which runs bridgetest three ways and powers off:

- through binfmt_misc with flags POFE, as a USERLAND=qemu-user guest's init registers qemu-arm:
  every check passes, bridge.userland is qemu-user, /proc/<pid>/exe names the program;
- under qemu-arm with QEMU_BRIDGE=0 (stock qemu-user): the uinput and DRM ioctls and the robust
  mutex fail, which shows the bridge is what makes them pass;
- natively in AArch32 EL0 (TCG's -cpu max has it): every check passes.

It needs qemu-arm (QEMU_ARM, else build/qemu-user/bin/qemu-arm: paths.Tree.qemu_arm), the guest
build's Image and initramfs, cpio, and a C compiler for static 32-bit ARM programs (BRIDGE_CC, else
the webOS SDK's or Debian's arm-linux-gnueabihf-gcc); it skips without them. About 30 s on the PC."""

import gzip
import os
import re
import shutil
import subprocess
import tempfile

import pytest
from beanjar import paths

TOP = paths.TREE.top
TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bridgetest.c")
CCS = (
    "arm-webos-linux-gnueabi-gcc",
    "/opt/arm-webos-linux-gnueabi_sdk-buildroot/bin/arm-webos-linux-gnueabi-gcc",
    "arm-linux-gnueabihf-gcc",
)
REGISTER = (
    r":qemu-arm:M::\x7fELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x28\x00"
    r":\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff"
    r":/bin/qemu-arm:POFE"
)
INIT = f"""#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc; mount -t sysfs sysfs /sys; mount -t devtmpfs devtmpfs /dev
echo "== stock"
QEMU_BRIDGE=0 /bin/qemu-arm /bin/bridgetest
echo "== native"
/bin/bridgetest
echo "== binfmt"
mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc
echo '{REGISTER}' > /proc/sys/fs/binfmt_misc/register
echo "bridge.binfmt_flags=$(sed -n 's/^flags: //p' /proc/sys/fs/binfmt_misc/qemu-arm)"
/bin/bridgetest
echo "== end"
poweroff -f
"""


def compiler():
    for cc in (os.environ.get("BRIDGE_CC"),) + CCS:
        if cc and shutil.which(cc):
            return cc
    return None


def sections(console):
    """{section: {key: value}} of the console's "== name" sections and bridge.* lines."""
    out, cur = {}, None
    for line in console.replace("\r", "").splitlines():
        m = re.match(r"== (\w+)$", line)
        if m:
            cur = out.setdefault(m.group(1), {})
        elif cur is not None and line.startswith("bridge."):
            k, _, v = line.partition("=")
            cur[k[len("bridge.") :]] = v
    return out


@pytest.fixture(scope="module")
def console():
    tree = paths.TREE
    qemu, qemu_args = tree.qemu()
    image, initramfs = tree.build("Image"), tree.build("initramfs.cpio.gz")
    arm = tree.qemu_arm()
    cc = compiler()
    for what, ok in (
        ("qemu-arm (QEMU_ARM or build/qemu-user)", arm),
        ("build/Image (scripts/build.sh guest)", os.path.isfile(image)),
        ("build/initramfs.cpio.gz", os.path.isfile(initramfs)),
        (qemu, shutil.which(qemu)),
        ("cpio", shutil.which("cpio")),
        ("a static 32-bit ARM C compiler (BRIDGE_CC)", cc),
    ):
        if not ok:
            pytest.skip(f"no {what}")
    tmp = tempfile.mkdtemp(prefix="beanjar-bridge-")
    try:
        root = os.path.join(tmp, "root")
        os.makedirs(os.path.join(root, "bin"))
        for d in ("dev", "proc", "sys", "tmp"):
            os.mkdir(os.path.join(root, d))
        subprocess.run(
            [cc, "-O2", "-static", "-pthread", TOOL, "-o", os.path.join(root, "bin/bridgetest")],
            check=True,
        )
        with gzip.open(initramfs) as f:
            packed = f.read()
        subprocess.run(
            ["cpio", "-idm", "--quiet", "bin/busybox"], input=packed, cwd=root, check=True
        )
        shutil.copyfile(arm, os.path.join(root, "bin/qemu-arm"))
        with open(os.path.join(root, "init"), "w") as f:
            f.write(INIT)
        for p in ("init", "bin/qemu-arm", "bin/busybox"):
            os.chmod(os.path.join(root, p), 0o755)
        cpio = os.path.join(tmp, "initrd.cpio")
        names = subprocess.run(
            ["find", "."], cwd=root, stdout=subprocess.PIPE, text=True, check=True
        ).stdout
        with open(cpio, "wb") as f:
            subprocess.run(
                ["cpio", "-o", "-H", "newc", "-R", "0:0", "--quiet"],
                cwd=root,
                input=names.encode(),
                stdout=f,
                check=True,
            )
        r = subprocess.run(
            [qemu, *qemu_args, "-M", "virt", "-cpu", "max,pauth=off", "-accel", "tcg",
             "-smp", "2", "-m", "1024", "-nographic", "-no-reboot",
             "-device", "virtio-gpu-pci", "-display", "none",
             "-kernel", image, "-initrd", cpio,
             "-append", "console=ttyAMA0 rdinit=/init loglevel=3"],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, errors="replace", timeout=300, check=False,
        )  # fmt: skip
        assert "== end" in r.stdout, r.stdout[-4000:]
        return r.stdout
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_binfmt_bridge(console):
    """The product path: binfmt_misc POFE to qemu-arm, the bridge on: all pass, exe is the program."""
    s = sections(console)["binfmt"]
    assert s["binfmt_flags"] == "POFE"
    assert s["userland"] == "qemu-user" and s["machine"] == "armv7l"
    assert s["failed"] == "0", s
    assert s["exe"] == "/bin/bridgetest"


def test_stock_qemu_user_fails(console):
    """Without the bridge qemu-user lacks what the bridge supplies."""
    s = sections(console)["stock"]
    assert s["userland"] == "qemu-user"
    for check in ("uinput-create", "drm-getresources", "virtgpu-getparam", "robust-mutex"):
        assert s[check].startswith("fail"), (check, s[check])


def test_native(console):
    s = sections(console)["native"]
    assert s["userland"] == "native"
    assert s["failed"] == "0", s
