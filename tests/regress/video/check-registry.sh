#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Does GStreamer pick up the video layer's plugins over a registry cache written with LG's?
# That is the case of a persistent state (/mnt/flash/data/gst_1_0_registry.arm.bin) from before
# the layer. Run from the initramfs debug shell of a running VM:
#   (the VM gets it with `host/scripts/vm-launcher put tests/regress/video/check-registry.sh` on the host)
#   sh /host/tests/regress/video/check-registry.sh
# It bind-mounts the stock libgstlxvdecbin/libgstadecsink (from the pristine lower rootfs, inside
# the guest only) over the layer's at their path, writes a scratch registry with them, removes
# the mounts and asks the same registry for vdecsink/adecsink again: GStreamer compares each
# plugin file's size and mtime with the cache and must rescan the two files.
D=/mnt/lg/res/lglib/gstreamer-1.0
if [ "${1:-}" != inside ]; then
	# Staged in webOS's own /tmp, which the initramfs cannot see: copy through a pipe.
	for p in lxvdecbin adecsink; do
		webos-enter sh -c "mkdir -p /tmp/stock-gst; cat > /tmp/stock-gst/libgst$p-1.0.so" < /lower/rootfs$D/libgst$p-1.0.so
	done
	webos-enter sh /mnt/host/tests/regress/video/check-registry.sh inside
	webos-enter rm -rf /tmp/stock-gst
	exit
fi
export GST_REGISTRY_1_0=/tmp/check-registry.bin GST_PLUGIN_PATH_1_0=/usr/lib/gstreamer-1.0:$D
export GST_PLUGIN_SCANNER_1_0=/usr/libexec/gstreamer-1.0/gst-plugin-scanner
rm -f "$GST_REGISTRY_1_0"
desc() { gst-inspect-1.0 "$1" 2>/dev/null | sed -n 's/^  Description  *//p' | head -1; }
for p in lxvdecbin adecsink; do mount --bind /tmp/stock-gst/libgst$p-1.0.so $D/libgst$p-1.0.so; done
echo "stock:  vdecsink '$(desc vdecsink)'  adecsink '$(desc adecsink)'"
for p in lxvdecbin adecsink; do umount $D/libgst$p-1.0.so; done
echo "cached: $(ls -l "$GST_REGISTRY_1_0" | awk '{print $5}') bytes"
v=$(desc vdecsink); a=$(desc adecsink)
echo "layer:  vdecsink '$v'  adecsink '$a'"
case "$v$a" in *emulator*emulator*) echo "PASS registry rescanned the replaced plugins" ;; *) echo "FAIL stale registry" ;; esac
rm -f "$GST_REGISTRY_1_0"
