# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# What the shims of the soc layer (guest/layers/soc/<chip>) and tvservice's private view of the OS
# (guest/layers/common/tvservice) share:
#   hal_common      static library (PIC): hal_log (hal-log.c), and the emulator's physical memory
#                   behind /dev/mem (physmem.c), whose one instance per process is in
#                   libtvservice-private.so. A library links only the members
#                   it uses: libhal_lg115x takes hal_log alone.
#   hal_link_stub   the empty link-time stand-in (link-stub.c) each shim links under its stock
#                   file's name, so that the shim records NEEDED on that file
# The frame buffers' place in the physical memory is the chip's (soc_phys_base,
# guest/layers/soc/<chip>/meson.build, read before this directory).
hal_common = static_library('hal_common', 'hal-log.c', 'physmem.c', pic: true,
  c_args: ['-DPHYS_BASE=' + soc_phys_base])
hal_link_stub = files('link-stub.c')
