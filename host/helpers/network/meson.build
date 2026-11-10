# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The mitmproxy venv for mode=mitm (requirements.txt, hash-checked): Debian 13 has no mitmproxy
# package (docs/dev/guides/build.md, "Host packages"). A venv cannot be moved, so it
# is made in place, in publish_dir (build/mitmproxy-venv), and only when the requirements change.
# The arm64 cross build (host_cross) makes none: a venv is this machine's Python's, and the packages
# carry none (an installed beanjar makes its own, `beanjar mitm setup`).
have_mitmproxy = get_option('mitmproxy').allowed() and not host_cross
if have_mitmproxy
  mitmproxy_venv = custom_target('mitmproxy-venv',
    input: 'requirements.txt',
    output: 'mitmproxy-venv.stamp',
    command: [venv_sh, publish_dir / 'mitmproxy-venv', '@INPUT@', '@OUTPUT@'],
    build_by_default: true)
  # for the unit tests that import cryptography or mitmproxy (tests/integration/network, tests/unit/securestore)
  mitmproxy_python = publish_dir / 'mitmproxy-venv/bin/python'
  env_prog = find_program('env')
endif

# link = "lan" (docs/dev/architecture/network.md): the one privileged step, a macvlan on the library's
# lan_parent made straight into the VM's rootless namespace. Installed with CAP_NET_ADMIN only
# (the package's postinst); published to build/network/ for the checkout, where it needs the same
# capability (sudo setcap cap_net_admin=ep build/network/beanjar-lan-helper) to work.
lan_helper = executable('beanjar-lan-helper', 'lan-helper.c',
  override_options: ['c_std=gnu11', 'warning_level=2', 'werror=true'],
  build_by_default: true)
published += [['file', lan_helper.full_path(), 'network/beanjar-lan-helper']]
publish_depends += lan_helper

# mode=mitm's port bridges (host/core/beanjar/netns.py): an epoll relay between tcp and unix
# sockets, one process per namespace (host, LAN, inner), published to build/network/.
relay = executable('beanjar-relay', 'relay.c',
  override_options: ['c_std=gnu11', 'warning_level=2', 'werror=true'],
  build_by_default: true)
published += [['file', relay.full_path(), 'network/beanjar-relay']]
publish_depends += relay
