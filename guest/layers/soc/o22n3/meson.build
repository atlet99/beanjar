# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# O22N3 keeps O22N2's boundaries; its boards are its own firmware's.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# Its ponytail media elements are W22O ponytail's builds (their build ids); a later release takes
# queue's.
if 'queue' in webos_reached
  gst_elements = 'lx-o22-queue'
elif 'ponytail' in webos_reached
  gst_elements = 'lx-o22-ponytail'
endif
