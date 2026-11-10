# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# LM21U, LG's StanbyME (N21D: webOS 6 kisscurl-kluane, webOS 24 ombre-okapi): LM21A's MStar/Mali
# boundary and media elements (webOS 6's GStreamer 1.16.2, as W21A's); its own boards.
# Its media elements are W21A's but for one departure (mstar-lm21u, src/video/src/omx.c MSTAR_N21D):
# omxaudiosink also takes the passthrough audio its stock decoders emit.
gst_elements = 'mstar-lm21u'
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
