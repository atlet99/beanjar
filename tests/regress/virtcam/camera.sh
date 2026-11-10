#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# In the guest, as root (tests/regress/virtcam/run-camera.py runs it over beanjar ares-shell --root): the
# USB camera VID:PID as the stack sees it, and a preview through LG's camera service for SECS
# seconds.
#
#   camera.sh VID PID SECS WIDTH HEIGHT FORMAT FPS
#
# Prints KEY=VALUE lines: the USB device (sysfs), the drivers of its interfaces and its V4L2 nodes,
# whether PDM lists it as a camera (deviceType CAM), camera2's id for it and getInfo's supported,
# then open, setFormat, startPreview (its shared memory key) and the service's own frame rate
# lines (cam_handle_(...) : fps(N), logged now and then) while the preview ran, and the stop. With
# SHM_FRAMES set to shm-frames (tests/regress/virtcam/shm-frames.c) its counts of the frames in the
# preview's shared memory come too (shm_*). Then '--- raw' and the Luna answers.
VID=$1 PID=$2 SECS=$3 WIDTH=$4 HEIGHT=$5 FORMAT=$6 FPS=$7
C=luna://com.webos.service.camera2
q() { luna-send -n 1 -a com.webos.app.browser "$@" </dev/null 2>&1 | tr -d '\n'; echo; }
raw=""
keep() { raw="$raw
### $1
$2"; }
finish() { echo "--- raw$raw"; exit 0; }

dev=""
n=0
while [ -z "$dev" ] && [ $n -lt 30 ]; do
	for d in /sys/bus/usb/devices/*; do
		[ "$(cat "$d/idVendor" 2>/dev/null):$(cat "$d/idProduct" 2>/dev/null)" = "$VID:$PID" ] && dev=$d
	done
	[ -n "$dev" ] || { sleep 1; n=$((n + 1)); }
done
echo "usb_device=${dev:+$(basename "$dev")}"
[ -n "$dev" ] || finish
echo "usb_speed=$(cat "$dev/speed")"
echo "usb_product=$(cat "$dev/product" 2>/dev/null)"
n=0
while [ $n -lt 15 ] && ! ls "$dev"/*/video4linux >/dev/null 2>&1; do sleep 1; n=$((n + 1)); done
for i in "$dev"/"$(basename "$dev")":*; do
	drv=$(readlink "$i/driver" 2>/dev/null)
	echo "driver=$(basename "$i") ${drv##*/}"
done
echo "video_nodes=$(ls "$dev"/*/video4linux 2>/dev/null | grep video | tr '\n' ' ')"

pdm=""
n=0
while [ $n -lt 60 ]; do
	pdm=$(q luna://com.webos.service.pdm/getAttachedNonStorageDeviceList '{"subscribe":false}')
	echo "$pdm" | grep -q "\"productID\":\"$PID\"[^}]*\"deviceType\":\"CAM\"\|\"deviceType\":\"CAM\"[^}]*\"productID\":\"$PID\"" && break
	sleep 1
	n=$((n + 1))
done
keep pdm "$pdm"
echo "pdm_cam=$(echo "$pdm" | grep -c "\"deviceType\":\"CAM\"")"

id=""
n=0
while [ -z "$id" ] && [ $n -lt 60 ]; do
	list=$(q $C/getCameraList '{}')
	id=$(echo "$list" | sed -n 's/.*"id": *"\([^"]*\)".*/\1/p')
	[ -n "$id" ] || { sleep 1; n=$((n + 1)); }
done
keep getCameraList "$list"
echo "camera_id=$id"
[ -n "$id" ] || finish
info=$(q $C/getInfo "{\"id\":\"$id\"}")
keep getInfo "$info"
echo "supported=$(echo "$info" | grep -c '"supported": *true')"
echo "name=$(echo "$info" | sed -n 's/.*"name": *"\([^"]*\)".*/\1/p')"

open=$(q $C/open "{\"id\":\"$id\",\"appId\":\"com.webos.app.browser\"}")
keep open "$open"
h=$(echo "$open" | sed -n 's/.*"handle": *\([0-9]*\).*/\1/p')
echo "handle=$h"
[ -n "$h" ] || finish
fmt=$(q $C/setFormat "{\"handle\":$h,\"params\":{\"width\":$WIDTH,\"height\":$HEIGHT,\"format\":\"$FORMAT\",\"fps\":$FPS}}")
keep setFormat "$fmt"
echo "set_format=$(echo "$fmt" | grep -c '"returnValue": *true')"
mark=$(wc -l </var/log/messages)
start=$(q $C/startPreview "{\"handle\":$h,\"params\":{\"type\":\"sharedmemory\",\"source\":\"0\"}}")
keep startPreview "$start"
key=$(echo "$start" | sed -n 's/.*"key": *\([0-9]*\).*/\1/p')
echo "preview_key=$key"
t0=$(cut -d' ' -f1 /proc/uptime)
# the frames the service writes to the preview's shared memory (shm-frames.c), or just the time
if [ -n "$key" ] && [ -x "$SHM_FRAMES" ]; then
	"$SHM_FRAMES" "$key" "$SECS"
else
	sleep "$SECS"
fi
t1=$(cut -d' ' -f1 /proc/uptime)
# the log may have rotated meanwhile: then all of the new file counts
now=$(wc -l </var/log/messages)
[ "$now" -ge "$mark" ] || mark=0
echo "service_fps=$(tail -n $((now - mark)) /var/log/messages | sed -n 's/.*cam_handle_([^)]*) *: *fps(\([0-9.]*\)).*/\1/p' | tr '\n' ' ')"
echo "preview_seconds=$(awk "BEGIN{print $t1 - $t0}")"
keep stopPreview "$(q $C/stopPreview "{\"handle\":$h}")"
keep close "$(q $C/close "{\"handle\":$h}")"
finish
