# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Consent-based installation of the pinned MITM tool into per-user data."""

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import sysconfig


class Error(RuntimeError):
    pass


def _run(command, *, stdout=None, stderr=None, text=False, capture_output=False, check=True):
    if capture_output:
        stdout = stderr = subprocess.PIPE
    child = subprocess.Popen(
        command, stdout=stdout, stderr=stderr, text=text, start_new_session=True
    )
    try:
        out, err = child.communicate()
    except BaseException:
        try:
            os.killpg(child.pid, signal.SIGTERM)
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()
        except ProcessLookupError:
            child.wait()
        raise
    if check and child.returncode:
        raise subprocess.CalledProcessError(child.returncode, command, output=out, stderr=err)
    return subprocess.CompletedProcess(command, child.returncode, out, err)


def root():
    return (
        Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share") / "beanjar/mitmproxy"
    )


def requirements(tree):
    return (
        Path("/usr/share/beanjar/mitmproxy/requirements.txt")
        if tree.installed
        else Path(tree.src("host/helpers/network/requirements.txt"))
    )


def fingerprint(tree):
    digest = hashlib.sha256(requirements(tree).read_bytes())
    digest.update((sys.implementation.cache_tag + ":" + sysconfig.get_platform()).encode())
    for name in ("mitm_addon.py", "mitm_transparent.py"):
        digest.update(Path(tree.src("host/helpers/network", name)).read_bytes())
    return digest.hexdigest()


def environment(tree):
    return root() / "environments" / fingerprint(tree)


def executable(tree):
    directory = environment(tree)
    try:
        current = (root() / "current").resolve(strict=True)
        if current == directory and (directory / ".validated").read_text().strip() == fingerprint(
            tree
        ):
            return str(directory / "bin/mitmweb")
    except OSError:
        pass
    # An unvalidated environment must never become a runnable path.
    return str(root() / "not-installed/mitmweb")


def available(tree):
    return os.access(executable(tree), os.X_OK)


def validate(directory, tree, log=None):
    pins = dict(re.findall(r"^([A-Za-z0-9_.-]+)==([^\s\\]+)", requirements(tree).read_text(), re.M))
    program = 'import importlib.metadata as m,json; print(json.dumps({d.metadata["Name"].lower().replace("_","-"):d.version for d in m.distributions()}))'
    result = _run(
        [str(directory / "bin/python"), "-c", program], capture_output=True, text=True, check=True
    )
    versions = json.loads(result.stdout)
    for name, version in pins.items():
        if versions.get(name.lower().replace("_", "-")) != version:
            raise Error(f"MITM dependency version mismatch: {name}")
    extra = (
        set(versions) - {n.lower().replace("_", "-") for n in pins} - {"pip", "setuptools", "wheel"}
    )
    if extra:
        raise Error("unexpected MITM dependencies: " + ", ".join(sorted(extra)))
    _run(
        [str(directory / "bin/mitmweb"), "--version"],
        check=True,
        stdout=log or subprocess.DEVNULL,
        stderr=log or subprocess.DEVNULL,
    )


def _in_use(directory):
    """Whether a process runs from directory: a running VM's mitmweb is its venv's python with the
    venv's bin/mitmweb as argument (any user namespace of ours shares this /proc)."""
    prefixes = tuple({str(directory) + "/", str(Path(directory).resolve()) + "/"})
    for pid in filter(str.isdigit, os.listdir("/proc")):
        try:
            args = Path(f"/proc/{pid}/cmdline").read_bytes().split(b"\0")
            exe = os.readlink(f"/proc/{pid}/exe")
        except OSError:
            continue
        if exe.startswith(prefixes) or any(
            a.decode(errors="replace").startswith(prefixes) for a in args
        ):
            return True
    return False


def prune(keep):
    """Remove the environments other than keep (the one current selects) that no process uses;
    the removed ones."""
    removed = []
    try:
        entries = sorted((root() / "environments").iterdir())
    except FileNotFoundError:
        return removed
    for directory in entries:
        if (
            directory == keep
            or directory.is_symlink()
            or not directory.is_dir()
            or _in_use(directory)
        ):
            continue
        shutil.rmtree(directory, ignore_errors=True)
        removed.append(directory)
    return removed


def prune_unused(tree):
    """prune() under the setup lock, keeping this tree's environment; the removed ones."""
    base = root()
    if not base.is_dir():
        return []
    with (base / ".setup.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return prune(environment(tree))


def install(tree):
    previous = signal.getsignal(signal.SIGTERM)

    def cancelled(_signal, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, cancelled)
    try:
        return _install(tree)
    finally:
        signal.signal(signal.SIGTERM, previous)


def _install(tree):
    base = root()
    base.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (base / ".setup.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        directory = environment(tree)
        if available(tree):
            validate(directory, tree)
            prune(directory)
            return directory
        if directory.exists():
            # Only this fingerprint's incomplete environment is discarded.
            shutil.rmtree(directory)
        directory.parent.mkdir(parents=True, exist_ok=True)
        logpath = base / "setup.log"
        try:
            with logpath.open("w") as log:
                _run(
                    ["/usr/bin/python3", "-m", "venv", str(directory)],
                    check=True,
                    stdout=log,
                    stderr=log,
                )
                _run(
                    [
                        str(directory / "bin/python"),
                        "-m",
                        "pip",
                        "install",
                        "--disable-pip-version-check",
                        "--require-hashes",
                        "--only-binary=:all:",
                        "--no-binary=pyperclip",
                        "-r",
                        str(requirements(tree)),
                    ],
                    check=True,
                    stdout=log,
                    stderr=log,
                )
                validate(directory, tree, log)
            (directory / ".validated").write_text(fingerprint(tree) + "\n")
            temporary = base / "current.new"
            temporary.unlink(missing_ok=True)
            temporary.symlink_to(directory.relative_to(base))
            temporary.replace(base / "current")
        except BaseException as exc:
            shutil.rmtree(directory, ignore_errors=True)
            if isinstance(exc, (KeyboardInterrupt, SystemExit)):
                raise
            raise Error(f"MITM setup failed; see {logpath}: {exc}") from exc
        # the earlier versions' environments, about 115 MB each, unless a running mitmweb uses one
        prune(directory)
        return directory


def ensure(tree, authorized=False):
    if available(tree):
        try:
            validate(environment(tree), tree)
            return True
        except (OSError, subprocess.SubprocessError, Error, ValueError) as exc:
            raise Error(
                f"MITM installation is invalid; remove {environment(tree)} and run beanjar mitm setup: {exc}"
            ) from exc
    if not authorized:
        if not sys.stdin.isatty():
            raise Error(
                "MITM is not installed. Run beanjar mitm setup --install (or beanjar mitm setup on a terminal) before starting the VM."
            )
        answer = input("Download and install pinned mitmproxy in Beanjar user data? [y/N] ")
        if answer.strip().lower() not in ("y", "yes"):
            raise Error("MITM installation declined; the VM remains stopped.")
    install(tree)
    return True


def main(argv=None):
    from . import paths

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--install",
        action="store_true",
        help="authorize downloading and installing the pinned dependencies",
    )
    args = parser.parse_args(argv)
    try:
        ensure(paths.TREE, args.install)
        # every successful setup, not only one that installs: the earlier versions' environments
        prune_unused(paths.TREE)
        print("MITM ready: " + executable(paths.TREE))
        return 0
    except Error as exc:
        print(str(exc), file=sys.stderr)
        return 1
