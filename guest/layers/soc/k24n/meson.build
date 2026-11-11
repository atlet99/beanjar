# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# K24N keeps K24's boundaries (Mali, libmali.so.0 with gbm: W25H's r51 exports libgbm's API, and
# bsppart adds a libgbm.so.1 wrapper NEEDing it); its boards are its own firmware's.
soc_layer_trees += ['--tree', meson.current_source_dir() / 'rootfs', '.']
