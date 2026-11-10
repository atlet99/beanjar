#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The installed headers of a GStreamer release, for compiling the video layer's plugins against the
# firmware's libgstreamer-1.0 (guest/layers/soc/src/video/meson.build runs this):
#
#   gst-headers.sh SRC OUT STAMP
#
# SRC is the unpacked gstreamer-<version> (subprojects/gstreamer.wrap: 1.2.4, the H13, M14 and M2
# firmwares'; gstreamer18.wrap: 1.8.0, W17H's; gstreamer112.wrap: 1.12.0, W18R's), whose name gives
# the version; OUT/gstreamer-1.0 receives
# the headers gst/Makefile.am and libs/gst/base/Makefile.am install, plus the few generated ones
# made here as the firmware's build configured them. STAMP is written last.
# Env: GLIB_MKENUMS (default glib-mkenums).
set -eu
src=$1 OUT=$2 STAMP=$3
version=${src##*/gstreamer-}
major=${version%%.*} rest=${version#*.}
minor=${rest%%.*} micro=${rest#*.}
INC=$OUT/gstreamer-1.0
rm -rf "$OUT"
mkdir -p "$INC/gst/base"
# The installed set, as listed in gst/Makefile.am and libs/gst/base/Makefile.am.
list() { sed -n "/^$1 =/,/^\$/p" "$2" | tr -d '\\' | sed "s/^$1 =//" | tr -s ' \t\n' ' '; }
headers=$(list gst_headers "$src/gst/Makefile.am")
for h in $headers math-compat.h; do cp "$src/gst/$h" "$INC/gst/"; done
for h in $(list 'libgstbase_@GST_API_VERSION@include_HEADERS' "$src/libs/gst/base/Makefile.am"); do
	cp "$src/libs/gst/base/$h" "$INC/gst/base/"
done
sed -e "s/@GST_VERSION_MAJOR@/$major/" -e "s/@GST_VERSION_MINOR@/$minor/" -e "s/@GST_VERSION_MICRO@/$micro/" \
	-e 's/@GST_VERSION_NANO@/0/' "$src/gst/gstversion.h.in" > "$INC/gst/gstversion.h"
# As configured for the firmware: debugging, parser, tracing, registry and plugins built in
# (libgstreamer exports gst_debug_log, gst_parse_launch, gst_alloc_trace_*), no unaligned access.
sed -e 's|@GST_DISABLE_\([A-Z_]*\)_DEFINE@|/* #undef GST_DISABLE_\1 */|' \
	-e 's|@GST_HAVE_UNALIGNED_ACCESS_DEFINE@|#define GST_HAVE_UNALIGNED_ACCESS 0|' \
	-e 's|@GSTCONFIG_BUILT_WITH_MSVC@|0|' \
	"$src/gst/gstconfig.h.in" > "$INC/gst/gstconfig.h"
# gstenumtypes.h exactly as gst/Makefile.am makes it (declarations only).
# shellcheck disable=SC2086
(cd "$src/gst" && "${GLIB_MKENUMS:-glib-mkenums}" \
	--fhead "#ifndef __GST_ENUM_TYPES_H__\n#define __GST_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "\n/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define GST_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n" \
	--ftail "G_END_DECLS\n\n#endif /* __GST_ENUM_TYPES_H__ */" \
	$headers) > "$INC/gst/gstenumtypes.h"
echo "/* GStreamer $version headers in $INC */" > "$STAMP"
