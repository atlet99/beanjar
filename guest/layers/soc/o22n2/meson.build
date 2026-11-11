# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# captureservice calls HAL directly. Its .2 surface ABI is the same 1056-byte
# layout as DILE: u16 width/height, format 0 for ARGB, and /dev/gfx offsets.
# Keep the supplied BSP as the dependency for exports outside the owned engine.
# The shim is served from the libdile_gal shim's engine: a firmware without a
# legacy DILE_GAL library (spacebuns) gets neither (guest/layers/soc/meson.build).
hal_stock += {
  'libhalgal': ['libhalgal.so.2', 'libhalgal.so.2.0.1',
               ['libhalgal.so', 'libhalgal.so.0'], 'bsppart'],
}
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
if 'spacebuns' in webos_reached
  # The legacy media path does not apply to Weston13 (spacebuns, and the releases after it).
  gst_plugin_dir = ''
endif
# The media elements are the release's (o22). Clear H.264/HEVC/AAC transport. The AAC-only plugin
# intentionally omits other stock codec factories; this is not complete default media qualification.
