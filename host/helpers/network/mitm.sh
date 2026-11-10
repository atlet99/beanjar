#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Install mitmproxy for mode=mitm (host/core/beanjar/qemu.py), once per machine:
#
#   host/helpers/network/mitm.sh setup          make build/mitmproxy-venv from requirements.txt
#
# The host build does the same (scripts/build.sh host, target mitmproxy-venv); this is the
# shortcut that needs nothing else. requirements.txt pins mitmproxy 11.0.2 (it says why) and its
# whole dependency closure with hashes.
# The mode=mitm namespace (host/core/beanjar/netns.py) runs mitmweb from this venv inside the network namespace, with the
# proxy's CA (host/helpers/network/tlsca.py: a library's <library>/ca, the checkout VM's local/ca) as its
# confdir, so ~/.mitmproxy is never used.
set -eu
TOP=$(cd "$(dirname "$0")/../../.." && pwd)
case ${1:-} in
setup) exec "$TOP/meson/venv.sh" "$TOP/build/mitmproxy-venv" "$TOP/host/helpers/network/requirements.txt" ;;
*) sed -n '2,5p' "$0"; exit 1 ;;
esac
