# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# O24 keeps O22N2's boundaries; its boards are its own firmware's.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
# Its stock media elements are builds of other O22-family firmwares (their build ids): ombre's are
# W24G ombre's, ponytail's and queue's video elements W22O's of that release.
if 'queue' in webos_reached
  gst_elements = 'lx-o22-queue'
elif 'ponytail' in webos_reached
  gst_elements = 'lx-o22-ponytail'
endif
