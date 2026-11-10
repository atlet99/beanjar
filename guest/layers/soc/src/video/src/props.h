// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Table-driven GObject properties for the media elements. The names, types and flags are those of
 * LG's elements (g_param_spec_* calls in their class_init, or gst-inspect-1.0's list where that is
 * the source), so that every g_object_set/get in
 * lpsink and libpf finds its property with the same value type: an unknown name would stop a
 * g_object_set varargs list at that point, and a wrong type would misread the varargs. Values
 * are stored and read back; only the ones the element computes are overridden by it. */
#ifndef STUB_PROPS_H
#define STUB_PROPS_H
#include <gst/gst.h>

/* G_TYPE_BOXED stands for a GstStructure (LG's write-only resource-info and fade). */
#define PROP_STRUCTURE G_TYPE_BOXED
/* G_TYPE_POINTER stands for a GValueArray of guint (MStar's mvsink "rectangle"). */
#define PROP_UINT_ARRAY G_TYPE_POINTER

struct prop_def {
    const char *name, *blurb;
    GType type;
    gint64 def;
    const char *str;          /* a string's default */
    GParamFlags flags;        /* G_PARAM_READABLE and/or G_PARAM_WRITABLE; 0: both */
    GType (*enum_type)(void); /* G_TYPE_ENUM: the enum's type */
    gint64 minimum, maximum;
    gboolean bounded; /* optional observed integer range */
};

/* One property, under the id given (props_install: the index in the table + 1). */
static inline void props_install_one(GObjectClass *klass, const struct prop_def *d, guint id)
{
    const GParamFlags rw = (d->flags ? d->flags : G_PARAM_READWRITE) | G_PARAM_STATIC_STRINGS;
    GParamSpec *spec;
    switch (d->type) {
    case G_TYPE_INT:
        spec = g_param_spec_int(d->name, d->name, d->blurb, d->bounded ? d->minimum : G_MININT,
                                d->bounded ? d->maximum : G_MAXINT, (gint)d->def, rw);
        break;
    case G_TYPE_UINT:
        spec = g_param_spec_uint(d->name, d->name, d->blurb, d->bounded ? d->minimum : 0,
                                 d->bounded ? d->maximum : G_MAXUINT, (guint)d->def, rw);
        break;
    case G_TYPE_UINT64:
        spec = g_param_spec_uint64(d->name, d->name, d->blurb, 0, G_MAXUINT64, (guint64)d->def, rw);
        break;
    case G_TYPE_INT64:
        spec = g_param_spec_int64(d->name, d->name, d->blurb, G_MININT64, G_MAXINT64, d->def, rw);
        break;
    case G_TYPE_ULONG:
        spec = g_param_spec_ulong(d->name, d->name, d->blurb, 0, G_MAXULONG, (gulong)d->def, rw);
        break;
    case G_TYPE_DOUBLE:
        spec = g_param_spec_double(d->name, d->name, d->blurb, -G_MAXDOUBLE, G_MAXDOUBLE,
                                   (gdouble)d->def, rw);
        break;
    case G_TYPE_ENUM:
        spec = g_param_spec_enum(d->name, d->name, d->blurb, d->enum_type(), (gint)d->def, rw);
        break;
    case PROP_UINT_ARRAY:
        spec = g_param_spec_value_array(
            d->name, d->name, d->blurb,
            g_param_spec_uint("element", "element", "element", 0, G_MAXUINT, 0, rw), rw);
        break;
    case G_TYPE_BOOLEAN:
        spec = g_param_spec_boolean(d->name, d->name, d->blurb, d->def != 0, rw);
        break;
    case PROP_STRUCTURE:
        spec = g_param_spec_boxed(d->name, d->name, d->blurb, GST_TYPE_STRUCTURE, rw);
        break;
    default:
        spec = g_param_spec_string(d->name, d->name, d->blurb, d->str, rw);
        break;
    }
    g_object_class_install_property(klass, id, spec);
}

static inline void props_install(GObjectClass *klass, const struct prop_def *defs, guint n)
{
    for (guint i = 0; i < n; ++i)
        props_install_one(klass, &defs[i], i + 1);
}

static inline void props_init(GValue *values, const struct prop_def *defs, guint n)
{
    for (guint i = 0; i < n; ++i) {
        g_value_init(&values[i], defs[i].type == PROP_STRUCTURE    ? GST_TYPE_STRUCTURE
                                 : defs[i].type == PROP_UINT_ARRAY ? g_value_array_get_type()
                                 : defs[i].type == G_TYPE_ENUM     ? defs[i].enum_type()
                                                                   : defs[i].type);
        switch (defs[i].type) {
        case G_TYPE_INT64:
            g_value_set_int64(&values[i], defs[i].def);
            break;
        case G_TYPE_ULONG:
            g_value_set_ulong(&values[i], (gulong)defs[i].def);
            break;
        case G_TYPE_DOUBLE:
            g_value_set_double(&values[i], (gdouble)defs[i].def);
            break;
        case G_TYPE_ENUM:
            g_value_set_enum(&values[i], (gint)defs[i].def);
            break;
        case G_TYPE_INT:
            g_value_set_int(&values[i], (gint)defs[i].def);
            break;
        case G_TYPE_UINT:
            g_value_set_uint(&values[i], (guint)defs[i].def);
            break;
        case G_TYPE_UINT64:
            g_value_set_uint64(&values[i], (guint64)defs[i].def);
            break;
        case G_TYPE_BOOLEAN:
            g_value_set_boolean(&values[i], defs[i].def != 0);
            break;
        case G_TYPE_STRING:
            g_value_set_static_string(&values[i], defs[i].str);
            break;
        default:
            break;
        }
    }
}

static inline void props_clear(GValue *values, guint n)
{
    for (guint i = 0; i < n; ++i)
        g_value_unset(&values[i]);
}

#endif
