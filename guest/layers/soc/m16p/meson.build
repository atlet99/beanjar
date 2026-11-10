# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The LG M16P (LG1313), W17H's SoC: the values the soc layer (guest/layers/soc/meson.build) and the
# common layer's hal_common are built with for this chip. Read before guest/layers/common; rootfs/ is this chip's part of the soc layer:
#   etc/init/gpu-libs.override, kdrivers-galcore.override, kdrivers-hdmi20.override,
#   kdrivers-vdec_cmn.override, kdrivers-venc_hxenc.override, firmware_vdec.override
#                                       bsppart's Mali, Vivante and LG kernel-driver jobs and the
#                                       rootfs's video firmware loader, nothing to load here
#                                       (bsppart's kdrivers job is already a no-op: the kernel
#                                       has the kdrivers built in, lg115x.ko here)
#   etc/emulator/board.cmdline          the bootloader's parameters (W17H lxboot's format)
#
# As on W16R, the board support is bsppart.pak's (targets.py, PAK_LOWERS bsppart:bsp): LG's
# libhal_* for LG1313 (.so.2.0.1, SONAME .so.2, with .so.0 links to the same files), libkadaptor
# (KADP over lg115x.ko's /proc/lg/base and /dev/lg/iic<n>), libhalgal, the Mali Midgard driver.

# The stock libraries the shims (guest/layers/soc/src/<name>) replace: name -> [the soname the shim takes,
# the stock file it NEEDs, [other names in usr/lib made links to the shim](, 'bsppart': the stock
# file is bsppart's, not the rootfs's). tvservice, securitymanager, remotediag, hdcp2, update,
# libdile_crypto and libstarfish-subtitle NEED libhal_lg115x.so.2 and libhal_crypto.so.2 (none
# records .so.0); bsppart's libhal_lg115x.so.2.0.1 exports every function the shim defines, and
# HAL_CRYPTO_ReadMskToSecureStore is unchanged. gm, libgm, libim and tvservice NEED
# libdile_gal.so.1, the rootfs's pass-through to bsppart's libhalgal.so.2 (W17H's has 39
# HAL_GAL_*; the surface is still 1056 bytes, the capability 76, whose last word, FBC support,
# gm leaves 0 when the shim fills the 72 it knows: no frame-buffer compression).
#
# Not built for M16P: libdile_vt. W17H's libdile_vt.so.0.1.0 is DILE_VT's newer interface (as
# M14's), which the shim does not implement.
hal_stock = {
  'libhal_lg115x': ['libhal_lg115x.so.2', 'libhal_lg115x.so.2.0.1', [], 'bsppart'],
  'libdile_gal': ['libdile_gal.so.1', 'libdile_gal.so.1.0.2', []],
  'libhal_crypto': ['libhal_crypto.so.2', 'libhal_crypto.so.2.0.1', [], 'bsppart'],
}
# Where the media elements (guest/layers/soc/src/video) go: the stock plugins' directory, and which:
# W17H's GStreamer is 1.8, and its LG elements are a decoder and a sink each (omx_lxvideodec and
# omx_lxvideosink, audio_dec and audio_sink), not h13's vdecsink/adecsink.
gst_plugin_dir = 'usr/lib/gstreamer-1.0'
gst_elements = 'lx'
# The emulator's frame buffers in tvservice's physical memory (guest/layers/common/hal-common/physmem.c;
# only the libdile_vt shim allocates there, which M16P does not build): 64 MiB from here. LG1313's
# registers start at 0xC0000000 (lxboot's device tree: the GIC at 0xc0001000), and the highest
# memory layout of its kernel's BASE_PLT_Init ends below 0xa8c00000; the graphics memory
# (include/webos-gfx.h) is 0xA8000000-0xB4010000 on every chip. Above that and below the
# registers.
soc_phys_base = '0xB8000000u'
# The GPU vendor libraries the gl layer replaces with empty stand-ins: none. bsppart's Mali
# Midgard r12p0 driver is one library, libmali.so.0.1 (no SONAME: its users record libmali.so),
# NEEDing libgm and Wayland; libEGL.so.1.4, libGLESv2.so.2.0, libGLESv1_CM.so.1.1 and
# libOpenCL.so.1.1 are small wrappers NEEDing it. Nothing imports a mali_*, ump_*, cl* or gbm_*
# symbol, and libmali NEEDs no libUMP any more, so no other stand-in is needed.
gl_vendor_stubs = []
# The vendor's one GL library (guest/layers/soc/h13/meson.build): libmali.so, NEEDing Mesa's libEGL
# libGLESv2 and libGLESv1_CM, as libmali.so.0.1 with its .so and .so.0 links (bsppart's names), as on M2.
# W17H's libwayland-egl.so.0.1 is a real implementation (NEEDing libgm) rather than a libmali
# wrapper; the gl layer puts Mesa's under that name on every chip.
gl_vendor_gl = 'mali'
gl_vendor_gl_needs = ['EGL', 'GLESv2', 'GLESv1_CM']

# this chip's part of the soc layer, after its base's (guest/layers/soc/meson.build)
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
