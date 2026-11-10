// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* gst-omx's registration from gstomx.conf, shared by MStar's (omx.c) and Realtek's (rtkomx.c)
 * libgstomx.so. */
#include "omxconf.h"
#include <syslog.h>

guint omx_conf_register(GstPlugin *plugin,
                        GType (*type_of)(const gchar *factory, const gchar *type_name),
                        void (*also)(GstPlugin *plugin, GKeyFile *conf, const gchar *section,
                                     GType type))
{
    static const gchar *env[] = {"GST_OMX_CONFIG_DIR", NULL};
    static const gchar *dirs[] = {"/etc/xdg", NULL};
    static const gchar *files[] = {"gstomx.conf", NULL};
    const gchar *dir = g_getenv("GST_OMX_CONFIG_DIR");
    gchar *path = g_build_filename(dir ? dir : "/etc/xdg", "gstomx.conf", NULL);
    GKeyFile *conf = g_key_file_new();
    GError *err = NULL;
    gchar **groups;
    guint registered = 0;

    /* the registry rescans this plugin when the file changes, as gst-omx asks */
    gst_plugin_add_dependency(plugin, env, dirs, files, GST_PLUGIN_DEPENDENCY_FLAG_NONE);
    if (!g_key_file_load_from_file(conf, path, G_KEY_FILE_NONE, &err)) {
        syslog(LOG_ERR, "omx: %s: %s", path, err->message);
        g_error_free(err);
        g_key_file_free(conf);
        g_free(path);
        return 0;
    }
    groups = g_key_file_get_groups(conf, NULL);
    for (gchar **g = groups; *g; g++) {
        gchar *type_name = g_key_file_get_string(conf, *g, "type-name", NULL);
        gint rank = g_key_file_get_integer(conf, *g, "rank", NULL);
        GType parent = type_name ? type_of(*g, type_name) : 0;
        if (!parent) {
            GST_WARNING("%s: no class %s", *g, type_name ? type_name : "(none)");
        } else {
            /* gst-omx's own type per factory: "<type-name>-<factory>" */
            gchar *sub = g_strdup_printf("%s-%s", type_name, *g);
            GTypeQuery q;
            GType t;
            g_type_query(parent, &q);
            t = g_type_from_name(sub);
            if (!t)
                t = g_type_register_static_simple(parent, sub, q.class_size, NULL, q.instance_size,
                                                  NULL, 0);
            if (gst_element_register(plugin, *g, rank, t)) {
                registered++;
                if (also)
                    also(plugin, conf, *g, t);
            }
            g_free(sub);
        }
        g_free(type_name);
    }
    GST_INFO("%u factories from %s", registered, path);
    g_strfreev(groups);
    g_key_file_free(conf);
    g_free(path);
    return registered;
}
