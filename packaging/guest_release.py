#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fetch the released beanjar-guest that pairs with this tree's host (packaging/debian/README.md,
"Built on GitHub").

  guest_release.py fetch --out DIR [--repo OWNER/NAME] [--top TOP]
  guest_release.py select [--repo OWNER/NAME] [--top TOP]

beanjar-guest is built and released only by the Guest workflow (.github/workflows/guest.yml), on a
tag guest-<version> of the public repository. This picks, among that repository's releases, the
newest whose version has the host-guest contract of TOP's packaging/VERSION (the range beanjar's
Depends names: >= 0.Y, << 0.(Y+1)), downloads its .deb, checks it against the release's SHA256SUMS
and writes it to DIR; select only prints that release's tag (a host release's notes name it). No
matching release is an error naming the tag to push. GH_TOKEN, when set,
authenticates the GitHub API requests; a public repository needs none.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

TOP = Path(__file__).resolve().parents[1]
REPOSITORY = "webosbrew/beanjar"
API = "https://api.github.com"
TAG_PREFIX = "guest-"
SUMS = "SHA256SUMS"


def _identity():
    spec = importlib.util.spec_from_file_location("beanjar_identity", TOP / "packaging/identity.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


identity = _identity()


class Error(Exception):
    pass


def order(version):
    """A sort key of a beanjar version: the semver, then the commits after it (dpkg's order)."""
    m = identity.VERSION.fullmatch(version)
    return (*map(int, m["base"].split(".")), int(m["count"] or 0))


def deb_name(version):
    return f"beanjar-guest_{version}_all.deb"


def x86_64_deb_name(version):
    return f"beanjar-guest-x86-64_{version}_amd64.deb"


def select(releases, contract):
    """The newest release (GitHub API objects) of beanjar-guest whose version has contract, with
    its version; None when there is none. Drafts and tags that are not guest-<version> are skipped."""
    found = []
    for release in releases:
        tag = release.get("tag_name") or ""
        version = tag[len(TAG_PREFIX) :]
        if (
            release.get("draft")
            or not tag.startswith(TAG_PREFIX)
            or not identity.semver.valid(version)
        ):
            continue
        if identity.contract(version) == contract:
            found.append((order(version), version, release))
    if not found:
        return None
    _, version, release = max(found, key=lambda f: f[0])
    return version, release


def parse_sums(text):
    """{file name: sha256} of a sha256sum listing."""
    sums = {}
    for line in text.splitlines():
        m = re.fullmatch(r"([0-9a-f]{64}) [ *](\S+)", line.strip())
        if m:
            sums[m[2]] = m[1]
    return sums


def _request(url, accept):
    req = urllib.request.Request(
        url, headers={"Accept": accept, "User-Agent": "beanjar-guest-release"}
    )
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        # not passed on to the storage host a download redirects to
        req.add_unredirected_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(req, timeout=120) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        hint = " (a private repository needs GH_TOKEN with read access)" if e.code == 404 else ""
        raise Error(f"{url}: HTTP {e.code} {e.reason}{hint}") from None
    except urllib.error.URLError as e:
        raise Error(f"{url}: {e.reason}") from None


def releases(repository):
    """Every release of repository, newest first as the API lists them."""
    found, page = [], 1
    while True:
        batch = json.loads(
            _request(
                f"{API}/repos/{repository}/releases?per_page=100&page={page}",
                "application/vnd.github+json",
            )
        )
        found += batch
        if len(batch) < 100:
            return found
        page += 1


def asset(release, name):
    """The bytes of release's asset name."""
    for a in release.get("assets", []):
        if a.get("name") == name:
            return _request(a["url"], "application/octet-stream")
    raise Error(f"release {release['tag_name']} has no asset {name}")


def pick(repository, host_version):
    """(version, release) of the newest beanjar-guest release of host_version's contract."""
    contract = identity.contract(host_version)
    picked = select(releases(repository), contract)
    if picked is None:
        raise Error(
            f"{repository} has no beanjar-guest release of contract {contract} "
            f"(versions >= {contract}, << {identity.next_contract(host_version)}): "
            f"push a tag {TAG_PREFIX}<version> on {repository} at the commit to release, "
            f"where <version> is the beanjar-guest version that commit builds "
            f"(the Guest workflow builds and releases it)"
        )
    return picked


def fetch(repository, host_version, out):
    """Download and check the newest beanjar-guest of host_version's contract, and its optional
    x86-64 kernel package, into out; the path of beanjar-guest."""
    version, release = pick(repository, host_version)
    sums = parse_sums(asset(release, SUMS).decode())
    debs = {}
    for name in (deb_name(version), x86_64_deb_name(version)):
        if name not in sums:
            raise Error(f"release {release['tag_name']}: {SUMS} does not list {name}")
        data = asset(release, name)
        digest = hashlib.sha256(data).hexdigest()
        if digest != sums[name]:
            raise Error(
                f"release {release['tag_name']}: {name} has SHA-256 {digest}, "
                f"{SUMS} says {sums[name]}"
            )
        debs[name] = data
    out.mkdir(parents=True, exist_ok=True)
    for name, data in debs.items():
        path = out / name
        tmp = path.with_name(name + ".part")
        tmp.write_bytes(data)
        os.replace(tmp, path)
    return out / deb_name(version)


def main(argv=None):
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = p.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("fetch")
    f.add_argument("--out", type=Path, required=True)
    f.add_argument("--repo", default=REPOSITORY)
    f.add_argument("--top", type=Path, default=TOP)
    s = sub.add_parser("select")
    s.add_argument("--repo", default=REPOSITORY)
    s.add_argument("--top", type=Path, default=TOP)
    a = p.parse_args(argv)
    try:
        host_version = identity.base_version(a.top)
        if a.cmd == "select":
            print(pick(a.repo, host_version)[1]["tag_name"])
            return 0
        path = fetch(a.repo, host_version, a.out)
    except (Error, identity.Error) as e:
        print(f"guest_release: {e}", file=sys.stderr)
        return 1
    print(f"{path}: SHA-256 checked against {SUMS}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
