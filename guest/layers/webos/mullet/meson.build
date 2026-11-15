# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# W22P mullet-mole (webOS 7): systemd 244.5 and the same BSP/pak mount
# layout as kisscurl. Inherit its service drop-ins for initial bring-up, apart from
# airplay-adaptor's: from here the adaptor itself keeps the airplay daemon off without LG's server
# switch, and multiviewcontroller needs its answer (rootfs/etc/systemd/system/airplay-adaptor.service.d).

webos_layer_args += ['--tree', meson.current_source_dir() / 'rootfs', '.']
