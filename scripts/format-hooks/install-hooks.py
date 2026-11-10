#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Install repository hooks while preserving the currently inherited hook directory."""

import os
from pathlib import Path
import shutil
import subprocess


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def main():
    root = Path(git("rev-parse", "--show-toplevel")).resolve()
    os.chdir(root)
    gitdir = Path(git("rev-parse", "--absolute-git-dir"))
    dispatch = gitdir / "format-hooks"
    inherited = Path(git("rev-parse", "--git-path", "hooks")).expanduser().resolve()
    missing = [name for name in ("pre-commit", "ruff", "clang-format") if not shutil.which(name)]
    if missing:
        raise SystemExit("Install the formatting tools on PATH first: " + ", ".join(missing))
    if inherited != dispatch:
        if dispatch.exists():
            raise SystemExit(f"Refusing to replace existing dispatch directory: {dispatch}")
        dispatch.mkdir()
        if inherited.is_dir():
            for hook in inherited.iterdir():
                if (
                    hook.is_file()
                    and os.access(hook, os.X_OK)
                    and not hook.name.endswith(".sample")
                ):
                    name = "pre-commit.inherited" if hook.name == "pre-commit" else hook.name
                    (dispatch / name).symlink_to(hook)
    shutil.copyfile(root / "scripts/format-hooks/pre-commit.sh", dispatch / "pre-commit")
    (dispatch / "pre-commit").chmod(0o755)
    subprocess.run(["git", "config", "--local", "core.hooksPath", str(dispatch)], check=True)
    print(f"Installed formatting hook in {dispatch}; inherited hooks remain in {inherited}.")


if __name__ == "__main__":
    main()
