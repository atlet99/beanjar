# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The Realtek "K6LP" (U-Boot's chip=K6LPA0, "LGE K6L Board"), W20L's SoC: a stub for the first
# systemd boot. The values the soc layer
# (guest/layers/soc/meson.build) and the common layer's hal_common are built with for this chip; rootfs/
# is this chip's part of the soc layer:
#   etc/systemd/system/gpu-libs.service.d/emulator-gpu.conf  bsppart's Mali modules (for
#                                Realtek's 4.4 kernel) not loaded; the unit succeeds
#   etc/emulator/board.cmdline   the bootloader's parameters: placeholders until step 4 derives
#                                W20L's own (decision C)
#
# W20L's board support is bsppart-arib.pak's (targets.py, PAK_LOWERS bsppart:bsp): Realtek's
# libkadaptor and libhal_rtd2872 over it, as on K5LP, and its systemd units (gpu-libs, kdrivers,
# webos-tv, aipq); Realtek's drivers are rtk.ko's (targets.py).

# W20L's rootfs libdile_gal (libim in the compositor, libgm-wayland's clients such as
# hybridtv-manager, factorymanager): the shim, as on k5lp. Stock, DILE_GAL_Init reaches bsppart's
# libkadaptor (KADP_GAL_Init, RHAL_SE_Init, se_open), which asserted in hybridtv-manager. W20L's one
# new entry point, SetFBHWControllerPathMode (factorymanager), stays LG's: its HAL only traces
# and answers 0.
# libdile_crypto: the model secret's read, as on K5LP (libsestore, then bsppart's libhal_crypto
# over the TEE, neither of which holds it here): without the shim nyx made a random NDUID at every
# open. K6HP, K7LP and the K8 chain inherit it.
hal_stock = {
  'libdile_gal': ['libdile_gal.so.1', 'libdile_gal.so.1.0.2', []],
  'libdile_crypto': ['libdile_crypto.so.0', 'libdile_crypto.so.0.1.1', []],
}
# libhalgal: from W20L on, captureservice calls HAL_GAL_* itself (bsppart's libhalgal.so, the
# .1.0.0 of every K6, K7 and K8 firmware, not DILE_GAL), and the stock HAL_GAL_Init's KADP_GAL_Init
# reaches RHAL_SE_Init's se_open, which asserts (se_drv.c:167) when /dev/se0 cannot be mapped:
# captureservice aborted at every start (K8 number1, before the shim). The shim serves HAL_GAL from
# the DILE_GAL software engine, as on queue and O22. K6HP, K7LP and the K8 chain inherit it.
hal_stock += {'libhalgal': ['libhalgal.so', 'libhalgal.so.1.0.0', ['libhalgal.so.1'], 'bsppart']}
# Where the media elements (guest/layers/soc/src/video) go: the stock plugins' directory, and which:
# Realtek's gst-omx decoders (libgstomx.so, by gstomx.conf's sections), dvovideosink and
# rtkalsasink, against W20L's GStreamer and GLib.
gst_plugin_dir = 'usr/lib/gstreamer-1.0'
gst_elements = 'rtk-k6lp'
# The emulator's frame buffers in physical memory (guest/layers/common/hal-common/physmem.c), K5LP's
# place: above the PAs rtk.ko hands out there.
soc_phys_base = '0x60000000u'
# The gl layer as k5lp's (scratch/w20l-graphics-study.md, section 1.3): bsppart's libmali.so (no
# soname) is the one GPU library, and it exports wl_egl_window_*, which libcbe and libwt import
# from it; W20L's does not NEED libgm. No other vendor library.
gl_vendor_stubs = []
gl_vendor_gl = 'mali'
gl_vendor_gl_needs = ['EGL', 'GLESv2', 'GLESv1_CM', 'wayland-egl']

# this chip's part of the soc layer (guest/layers/soc/meson.build)
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
