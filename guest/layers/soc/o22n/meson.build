# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# O22N keeps the initial LG GAL boundary; board options are firmware-derived.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# W23O number1's AAC drc-mode default differs from W22O's; from ombre on, the release's (o22)
if 'ombre' not in webos_reached
  gst_elements = 'lx-o22n'
endif
