# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The LG M14 (LG1156), W15M's SoC: the values the soc layer (guest/layers/soc/meson.build) and the
# common layer's hal_common are built with for this chip. Its base is H13 (base: guest/layers/soc/h13 is read first): H13's jobs, with M14's board.cmdline
# (rootfs/), stock libraries and media directory; the frame buffers' place and the PowerVR
# stand-ins are H13's.

# The stock libraries the shims (guest/layers/soc/src/<name>) replace: name -> [the soname the shim takes,
# the stock file it NEEDs, [other names in usr/lib made links to the shim]]. W15M's rootfs ships
# each HAL library once, as <name>.so.2.0.0 (SONAME <name>.so.2), with the old generation's names
# .so.0 and .so.0.1.0 as links to it. tvservice and securitymanager NEED libhal_lg115x.so.2 and
# libhal_crypto.so.2; remotediag alone NEEDs libhal_crypto.so.0, which becomes a link to the shim
# so that it cannot load LG's file without it. gm, libgm, libim and tvservice NEED
# libdile_gal.so.1 (unchanged from H13). All the functions the three shims define are exported by
# these files.
#
# Not built for M14: libdile_vt. W15M's libdile_vt.so.0.1.0 is DILE_VT's newer interface (it adds
# DILE_VT_Init, _Start, _Stop, _Finalize, _GetInputVideoInfo, _WaitVsync, ... over H13's and takes
# libhal_vt.so.2 below it); the shim implements the older one. tv.capture's video plane is LG's library until then.
hal_stock = {
  'libhal_lg115x': ['libhal_lg115x.so.2', 'libhal_lg115x.so.2.0.0', []],
  'libdile_gal': ['libdile_gal.so.1', 'libdile_gal.so.1.0.2', []],
  'libhal_crypto': ['libhal_crypto.so.2', 'libhal_crypto.so.2.0.0', ['libhal_crypto.so.0']],
}
# Where the media elements (guest/layers/soc/src/video) go: W15M keeps libgstlxvdecbin-1.0.so and
# libgstadecsink-1.0.so with GStreamer's own plugins (GStreamer 1.2.4 and GLib 2.34.3, as WT1H).
gst_plugin_dir = 'usr/lib/gstreamer-1.0'
# The emulator's frame buffers (soc_phys_base) at H13's place: on M14 B0 the DDR ends at
# 0xA0000000 (LG's kdriver platform_init.c for M14: DDR1's display area 0x90000000-0x9FFFFFFF) and
# the registers start at 0xC0000000 (lg1156.dts; the GFX block at 0xC000B000), so the hole is the
# same. The PowerVR (SGX) libraries the gl layer replaces are H13's (gl_vendor_stubs): W15M's
# usr/lib has libIMGegl.so.0.1, libsrv_um.so.0.1 and libglslcompiler.so.0.1 with their links; no
# vendor GL library by another name.

# this chip's part of the soc layer, after its base's (guest/layers/soc/meson.build)
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
