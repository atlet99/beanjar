#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The coverage report of a coverage build (`ninja -C build/coverage coverage-report`, after its
`meson test`; docs/dev/guides/coverage.md): gcovr for the C host programs (meson/coverage/gcovr.cfg picks the files),
coverage.py for the manager (beanjar), both per file with lines and branches, into
meson-logs/coverage-report/ of the build: summary.txt (the table it prints), c/index.html and
python/index.html.

    report.py SOURCE_ROOT BUILD_ROOT
"""

import json
import os
import shutil
import subprocess
import sys


def pct(n, total):
    return f"{100 * n / total:5.1f}%" if total else "    -"


def row(name, lines, lines_total, branches, branches_total):
    return (
        f"{name:<52} {pct(lines, lines_total)} {lines:>5}/{lines_total:<5} "
        f"{pct(branches, branches_total)} {branches:>5}/{branches_total:<5}"
    )


def table(title, files):
    """files: [(path, lines, lines_total, branches, branches_total)], grouped by directory."""
    out = [title, f"{'file':<52} {'lines':>19} {'branches':>18}"]
    groups = {}
    for f in sorted(files):
        groups.setdefault(os.path.dirname(f[0]), []).append(f)
    for d, fs in groups.items():
        for f in fs:
            out.append(row(*f))
        if len(fs) > 1:
            out.append(row(f"  {d}/ (all)", *[sum(f[i] for f in fs) for i in range(1, 5)]))
    out.append(row("TOTAL", *[sum(f[i] for f in files) for i in range(1, 5)]))
    return out


def c_report(top, build, dest):
    os.makedirs(os.path.join(dest, "c"), exist_ok=True)
    summary = os.path.join(dest, "c-summary.json")
    # meson/coverage/gcovr.cfg: the host programs' files only
    subprocess.run(
        [
            "gcovr",
            "-r",
            top,
            "--config",
            os.path.join(top, "meson", "coverage", "gcovr.cfg"),
            build,
            "--json-summary",
            summary,
            "--html-details",
            os.path.join(dest, "c", "index.html"),
        ],
        check=True,
    )
    with open(summary) as f:
        data = json.load(f)
    return [
        (f["filename"], f["line_covered"], f["line_total"], f["branch_covered"], f["branch_total"])
        for f in data["files"]
    ]


def python_report(top, build, dest):
    rc = os.path.join(build, "meson", "coverage", "coveragerc")
    data = os.path.join(build, "meson-logs", "coverage-python")
    cov = [sys.executable, "-m", "coverage"]
    env = dict(os.environ, COVERAGE_RCFILE=rc)
    env.pop("COVERAGE_PROCESS_START", None)
    parts = [
        n for n in (os.listdir(data) if os.path.isdir(data) else []) if n.startswith(".coverage.")
    ]
    if parts:
        # parallel files of this test run into .coverage (an earlier combined one is replaced)
        if os.path.exists(os.path.join(data, ".coverage")):
            os.unlink(os.path.join(data, ".coverage"))
        subprocess.run(cov + ["combine", "-q"], env=env, cwd=data, check=True)
    if not os.path.exists(os.path.join(data, ".coverage")):
        print("coverage-report: no coverage.py data (run meson test first)", file=sys.stderr)
        return []
    out = os.path.join(dest, "python.json")
    shutil.rmtree(os.path.join(dest, "python"), ignore_errors=True)
    subprocess.run(
        cov + ["html", "-q", "-d", os.path.join(dest, "python")], env=env, cwd=data, check=True
    )
    subprocess.run(cov + ["json", "-q", "-o", out], env=env, cwd=data, check=True)
    with open(out) as f:
        files = json.load(f)["files"]
    return [
        (
            os.path.relpath(p, top),
            s["covered_lines"],
            s["num_statements"],
            s["covered_branches"],
            s["num_branches"],
        )
        for p, s in ((p, v["summary"]) for p, v in files.items())
    ]


def main():
    top, build = (os.path.abspath(a) for a in sys.argv[1:3])
    dest = os.path.join(build, "meson-logs", "coverage-report")
    os.makedirs(dest, exist_ok=True)
    lines = table("C host programs (gcov; branches: taken)", c_report(top, build, dest))
    lines += [""] + table(
        "Python manager (coverage.py; branches: arcs taken)", python_report(top, build, dest)
    )
    text = "\n".join(lines) + "\n"
    with open(os.path.join(dest, "summary.txt"), "w") as f:
        f.write(text)
    print(text, end="")
    print(f"HTML: {dest}/c/index.html, {dest}/python/index.html")


if __name__ == "__main__":
    main()
