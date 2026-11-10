// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* gst-omx's registration (omxconf.c): one factory per section of gstomx.conf. */
#ifndef OMXCONF_H
#define OMXCONF_H
#include <gst/gst.h>

/* Registers a factory for each section of $GST_OMX_CONFIG_DIR (or /etc/xdg)/gstomx.conf, as
 * gst-omx does: the section's name, at its rank, of a type "<type-name>-<factory>" derived from
 * the class type_of returns for the section's type-name (0: no such class, the section is left
 * out). also, if not NULL, is then called with each registered section and its type, for the
 * factories a vendor's plugin derives from a section (rtkomx.c). The plugin is rescanned when the
 * file changes. */
__attribute__((visibility("hidden"))) guint omx_conf_register(
    GstPlugin *plugin, GType (*type_of)(const gchar *factory, const gchar *type_name),
    void (*also)(GstPlugin *plugin, GKeyFile *conf, const gchar *section, GType type));

#endif
