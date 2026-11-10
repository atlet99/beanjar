#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Inside webOS: direct calls to tvservice's media services (videosinkmanager, tv.display,
# tv.avblock) on a scratch context, as the pipeline and WebKit make them, plus error cases; and
# what each reaches below tvservice (guest/layers/soc/h13's HAL shim, "tvservice-hal: video" lines in
# /var/log/legacy-log). Leaves nothing behind.
#   (the VM gets it with `host/scripts/vm-launcher put tests/regress/video/luna-media.sh` on the host)
#   webos-enter sh /mnt/host/tests/regress/video/luna-media.sh
# Replies as LG's tvservice gives them (checked 2026-09-28): register, connect, setDisplayWindow,
# the AV mutes, disconnect and unregister answer returnValue true with the context;
# setMediaVideoData answers ERROR_06 for this scratch context and payload (the pipeline's own call
# succeeds: tests/regress/video/check-video-app.sh gets to "playing", which waits for it); connect with
# sinkType PIP, register without a context and setDisplayWindow without displayOutput answer
# ERROR_06, ERROR_06 and DISPLAY_ERROR_0001; an unknown context's unregister
# VIDEOSINKMANAGER_ERROR_99. Only setDisplayWindow is on the public bus. Below it: connect opens and connects the MAIN scaler window to VDEC 1, setDisplayWindow sets
# its output region, the AV mutes blank and unblank it, disconnect blanks, disconnects and closes.
V=luna://com.webos.service.videosinkmanager
D=luna://com.webos.service.tv.display
A=luna://com.webos.service.tv.avblock
c=_lunaMediaTest
call() { echo "### $1 $2"; timeout -t 10 luna-send -n 1 "$1" "$2" </dev/null 2>&1; echo; }
start=$(wc -l < /var/log/legacy-log)
call $V/private/register "{\"context\":\"$c\",\"resourceList\":[{\"type\":\"VDEC\",\"portNumber\":1}],\"audioType\":\"media\"}"
call $V/getConnectionState "{\"context\":\"$c\"}"
call $V/connect "{\"context\":\"$c\",\"sinkType\":\"MAIN\"}"
call $V/getConnectionState "{\"context\":\"$c\"}"
call $D/setDisplayWindow "{\"context\":\"$c\",\"displayOutput\":{\"positionX\":100,\"positionY\":50,\"width\":640,\"height\":360},\"fullScreen\":false}"
call $D/private/setMediaVideoData "{\"context\":\"$c\",\"content\":\"movie\",\"video\":{\"frameRate\":30,\"scanType\":\"VIDEO_PROGRESSIVE\",\"width\":1280,\"height\":720,\"bitRate\":0,\"adaptive\":false,\"seamlessMode\":\"none\",\"pixelAspectRatio\":{\"width\":1,\"height\":1}}}"
call $A/startAvMute "{\"context\":\"$c\",\"muteOwner\":\"test\"}"
call $A/stopAvMute "{\"context\":\"$c\",\"muteOwner\":\"test\"}"
call $V/disconnect "{\"context\":\"$c\"}"
call $V/private/unregister "{\"context\":\"$c\"}"
echo "#### errors"
call $V/connect "{\"context\":\"$c\",\"sinkType\":\"PIP\"}"
call $V/private/register '{}'
call $D/setDisplayWindow "{\"context\":\"$c\",\"fullScreen\":false}"
echo "#### public bus (setDisplayWindow only; the rest is private)"
echo "### luna-send-pub $D/setDisplayWindow"; timeout -t 10 luna-send-pub -n 1 $D/setDisplayWindow "{\"context\":\"$c\",\"fullScreen\":true}" </dev/null 2>&1; echo
echo "### luna-send-pub $V/getConnectionState (expect: not permitted)"
timeout -t 5 luna-send-pub -n 1 $V/getConnectionState "{\"context\":\"$c\"}" </dev/null 2>&1; echo
call $V/private/unregister "{\"context\":\"$c\"}"
echo "#### below tvservice"
tail -n +$((start + 1)) /var/log/legacy-log | grep 'tvservice-hal: video' | sed 's/.*tvservice-hal: //'
