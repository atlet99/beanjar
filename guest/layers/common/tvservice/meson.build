# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# usr/lib/libtvservice-private.so, in the common layer: LG's real tvservice (the stock job, the
# tvservice pak's lgapp/tvservice) with its private view of the OS: tvservice's own /proc/partinfo, nvram partition and physical memory
# behind /dev/mem, whose graphics range is /dev/gfx, and the return from the division by zero in
# tv.capture's VIDEO method with no signal (libc-private.c; the physical memory is
# guest/layers/common/hal-common/physmem.c). It exports only what libtvservice-private.map lists. The
# jobs that preload it are copies of the release's own and are the webos layer's
# (guest/layers/webos/<release>: etc/init/tvservice.override, etc/init/tvwin.override and tvwin's LS2
# service files). The library takes itself out of LD_PRELOAD when it is loaded, so the processes
# these start see the stock environment. The HAL and DILE shims under tvservice are system
# libraries, in the soc layer (guest/layers/soc/<chip>).
tvservice_private_map = files('libtvservice-private.map')
tvservice_private = shared_library('tvservice-private', 'libc-private.c',
  link_with: hal_common,
  link_args: ['-Wl,--version-script=' + tvservice_private_map[0].full_path(), '-ldl', '-lpthread'],
  link_depends: tvservice_private_map)
