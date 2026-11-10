# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Explicitly invoked for LM14 bring-up; independent of the optional custom QEMU.
if host_machine.system() == 'linux' and host_machine.cpu_family() == 'aarch64'
  haltpoll = shared_library('kvm-haltpoll', 'haltpoll.c', 'haltpoll-aarch64.S',
    override_options: ['c_std=gnu11', 'warning_level=2', 'werror=true'],
    build_by_default: true)
  published += [['file', haltpoll.full_path(), 'kvm/libkvm-haltpoll.so']]
  publish_depends += haltpoll
endif
