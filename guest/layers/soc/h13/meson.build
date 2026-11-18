# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The LG H13 (LG1154), WT1H's SoC: the values the soc layer (guest/layers/soc/meson.build) and the
# common layer's hal_common are built with for this chip. Read before guest/layers/common; rootfs/ is this chip's part of the soc layer:
#   etc/init/gpu-libs.override, kdrivers-vdec_cmn.override, kdrivers-venc_hxenc.override
#                                       the kernel-driver and PowerVR jobs, nothing to load here
#   etc/emulator/board.cmdline          the bootloader's parameters (H13 lxboot's format)
# M14 (guest/layers/soc/m14) names this directory its base: what is here is M14's unless it says otherwise.

# The stock libraries the shims (guest/layers/soc/src/<name>) replace: name -> [the soname the shim takes,
# the stock file it NEEDs, [other names in usr/lib made links to the shim]]. WT1H's rootfs has one
# generation of each, and every process links the soname. A shim not named is not built.
hal_stock = {
  'libhal_lg115x': ['libhal_lg115x.so.0', 'libhal_lg115x.so.0.0.0', []],
  'libdile_vt': ['libdile_vt.so.0', 'libdile_vt.so.0.1.0', []],
  'libdile_gal': ['libdile_gal.so.1', 'libdile_gal.so.1.0.2', []],
  'libhal_crypto': ['libhal_crypto.so.0', 'libhal_crypto.so.0.1.0', []],
}
# Where the media elements (guest/layers/soc/src/video) go: the stock plugins' directory.
gst_plugin_dir = 'mnt/lg/res/lglib/gstreamer-1.0'
# The emulator's frame buffers in tvservice's physical memory (guest/layers/common/hal-common/physmem.c):
# 64 MiB from here, in the hole between LG1154's DDR (below 2 GiB) and its register space
# (0xC0000000).
soc_phys_base = '0xA0000000u'
# The PowerVR libraries the gl layer replaces with empty stand-ins (guest/layers/gl), each as
# lib<name>.so.0.1 with its .so and .so.0 links.
gl_vendor_stubs = ['IMGegl', 'srv_um', 'glslcompiler']
# The vendor's one GL library (EGL and GLES in one file) the firmware links by name, which the gl
# layer stands in for with a library of that name NEEDing Mesa's libEGL and libGLESv2; '' for
# none (PowerVR's EGL and GLES are separate libraries of the standard names).
gl_vendor_gl = ''

# this chip's part of the soc layer, after its base's (guest/layers/soc/meson.build)
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# The OpenGL ES version of the chip's GPU (gl_es_version), at most which the gl layer's Mesa
# gives a context: WT1H 05.06.10: usr/lib/libGLESv2.so.2.0 prints "OpenGL ES 3.0 build 1.3@2793094"
# (PowerVR Rogue Han)
gl_es_version = '3.0'
