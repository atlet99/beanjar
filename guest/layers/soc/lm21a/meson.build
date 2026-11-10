# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# W21A's LM21A (MSD96EBDW), webOS 6.5.2 kisscurl-koli. Its DILE graphics
# interface and Mali SONAME are the release's existing Mesa integration points.
# The newer MLP driver ABI is brought up independently of M3 and K7LP.
hal_stock = {
  'libdile_gal': ['libdile_gal.so.1', 'libdile_gal.so.1.0.2', []],
  'libdile_crypto': ['libdile_crypto.so.0', 'libdile_crypto.so.0.1.1', []],
}
# The media elements under MStar's stock names, as on M3: the stock gst-omx decoders open the
# MStar video device (/dev/video20) and fail, so nothing reached the host.
gst_plugin_dir = 'usr/lib/gstreamer-1.0'
gst_elements = 'mstar-lm21a'
soc_phys_base = '0x40000000u'
gl_vendor_stubs = []
gl_vendor_gl = 'mali'
gl_vendor_gl_soname = 'libmali.so.0'
gl_vendor_gl_needs = ['EGL', 'GLESv2', 'GLESv1_CM']
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
