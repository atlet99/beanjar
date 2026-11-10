#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The test clips of the media checks (tests/regress/video/check-media.sh), made with the host's ffmpeg (libx264, libx265, libvpx, libmp3lame; --decode also
# libsvtav1 and libopus) into OUT
# (default build/media-test). Never committed. 20 s each, testsrc2 pictures and a 1 kHz tone that
# beeps each second, except av-sync.mp4: black with a white frame and a 50 ms beep at each second
# (for WEBOS_MEDIA_PROBE=1).
#   tests/regress/video/make-clips.sh [OUT]
#   tests/regress/video/make-clips.sh --decode OUT
# --decode: the clips of scripts/regress --tv's decode step (tests/regress/lib/tvsuite.py DECODE_CASES),
# 20 s at 960x540 25 fps, each with its case's name and the media time burned in over testsrc2 and a
# beeping 1 kHz tone, into OUT/media/: for <video> one file per case, for MSE (tvapp's player) an
# init segment and 2 s media segments per track (ffmpeg's dash muxer), video and audio apart; and
# OUT/media/cases.js, the page's table of them (DECODE: files, MIME types, codec strings).
set -eu
f() { ffmpeg -hide_banner -loglevel error -y "$@"; }
if [ "${1:-}" = --decode ]; then
	OUT=${2:?usage: make-clips.sh --decode OUT}/media
	mkdir -p "$OUT"
	cd "$OUT"
	font=/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf
	# vid CASE: the picture, with CASE and the media time when there is a font
	vid() {
		t=
		[ -f $font ] && t=",drawtext=fontfile=$font:text='$1 %{pts\\:hms}':fontsize=48:fontcolor=white:box=1:boxcolor=black@0.7:x=(w-tw)/2:y=h-th-30"
		echo "testsrc2=size=960x540:rate=25$t"
	}
	A="-f lavfi -i sine=frequency=1000:beep_factor=4:sample_rate=48000 -ac 2"
	H264="-c:v libx264 -profile:v main -level 3.1 -pix_fmt yuv420p -g 50 -b:v 800k"
	HEVC="-c:v libx265 -x265-params log-level=error:keyint=50:min-keyint=50:level-idc=31 -pix_fmt yuv420p -tag:v hvc1 -b:v 600k"
	VP9="-c:v libvpx-vp9 -b:v 800k -g 50 -deadline realtime -cpu-used 8"
	AV1="-c:v libsvtav1 -preset 10 -g 50 -b:v 600k -pix_fmt yuv420p"
	OPUS="-c:a libopus -b:a 96k"
	export SVT_LOG=1 # SVT-AV1 prints its banner otherwise
	f -f lavfi -i "$(vid 'file H.264+AAC')" $A -t 20 $H264 -c:a aac -b:a 96k -shortest -movflags +faststart file-h264.mp4
	f -f lavfi -i "$(vid 'file HEVC+AAC')" $A -t 20 $HEVC -c:a aac -b:a 96k -shortest -movflags +faststart file-hevc.mp4
	f -f lavfi -i "$(vid 'file VP9')" -t 20 $VP9 file-vp9.webm
	f -f lavfi -i "$(vid 'file MPEG-2+AAC TS')" $A -t 20 -c:v mpeg2video -b:v 1200k -g 25 -c:a aac -b:a 96k -shortest file-mpeg2.ts
	f -f lavfi -i "$(vid 'file VP9+Opus')" $A -t 20 $VP9 $OPUS -shortest file-vp9-opus.webm
	f -f lavfi -i "$(vid 'file AV1+Opus')" $A -t 20 $AV1 $OPUS -shortest file-av1.webm
	# seg IN MAP NAME [webm]: IN's MAP track as NAME-init.<ext> and NAME-<n>.<ext>
	seg() {
		ext=m4s init=mp4 type=
		[ "${4:-}" = webm ] && ext=webm init=webm type="-dash_segment_type webm"
		f -i "$1" -map "$2" -c copy -f dash $type -seg_duration 2 -use_template 0 -use_timeline 0 \
			-init_seg_name "$3-init.$init" -media_seg_name "$3-\$Number\$.$ext" "$3.mpd"
		rm -f "$3.mpd"
	}
	f -f lavfi -i "$(vid 'MSE H.264+AAC')" $A -t 20 $H264 -c:a aac -b:a 96k -shortest mse-h264.mp4
	seg mse-h264.mp4 0:v mse-h264-v; seg mse-h264.mp4 0:a mse-h264-a
	f -f lavfi -i "$(vid 'MSE HEVC+AAC')" $A -t 20 $HEVC -c:a aac -b:a 96k -shortest mse-hevc.mp4
	seg mse-hevc.mp4 0:v mse-hevc-v; seg mse-hevc.mp4 0:a mse-hevc-a
	f -f lavfi -i "$(vid 'MSE VP9')" -t 20 $VP9 mse-vp9.webm
	seg mse-vp9.webm 0:v mse-vp9-v webm
	f -f lavfi -i "$(vid 'MSE VP9+Opus')" $A -t 20 $VP9 $OPUS -shortest mse-vp9opus.webm
	seg mse-vp9opus.webm 0:v mse-vp9opus-v webm; seg mse-vp9opus.webm 0:a mse-vp9opus-a webm
	f -f lavfi -i "$(vid 'MSE AV1+Opus')" $A -t 20 $AV1 $OPUS -shortest mse-av1.webm
	seg mse-av1.webm 0:v mse-av1-v webm; seg mse-av1.webm 0:a mse-av1-a webm
	rm -f mse-h264.mp4 mse-hevc.mp4 mse-vp9.webm mse-vp9opus.webm mse-av1.webm
	# segs NAME: the page's list of NAME's files, init first
	segs() {
		printf '["media/%s"' "$(ls "$1"-init.* | head -n 1)"
		ls "$1"-[0-9]*.* | sort -t- -k4 -n | while read -r x; do printf ',"media/%s"' "$x"; done
		printf ']'
	}
	# AV1: main profile, level 3.0 (seq_level_idx 4, which 960x540 at 25 fps fits), 8 bits
	AVC='avc1.4d401f' AAC='mp4a.40.2' HVC='hvc1.1.6.L93.B0' AV01='av01.0.04M.08'
	{
		echo "// made by tests/regress/video/make-clips.sh --decode; the decode step's cases (tvsuite.DECODE_CASES)"
		echo "var DECODE = {"
		echo " \"file.h264\": {\"src\": \"media/file-h264.mp4\", \"mime\": \"video/mp4; codecs=\\\"$AVC, $AAC\\\"\"},"
		echo " \"file.hevc\": {\"src\": \"media/file-hevc.mp4\", \"mime\": \"video/mp4; codecs=\\\"$HVC, $AAC\\\"\"},"
		echo " \"file.vp9\": {\"src\": \"media/file-vp9.webm\", \"mime\": \"video/webm; codecs=\\\"vp9\\\"\"},"
		echo " \"file.mpeg2\": {\"src\": \"media/file-mpeg2.ts\", \"mime\": \"video/mp2t\"},"
		echo " \"file.vp9-opus\": {\"src\": \"media/file-vp9-opus.webm\", \"mime\": \"video/webm; codecs=\\\"vp9, opus\\\"\"},"
		echo " \"file.av1\": {\"src\": \"media/file-av1.webm\", \"mime\": \"video/webm; codecs=\\\"$AV01, opus\\\"\"},"
		echo " \"mse.h264\": {\"mse\": [{\"mime\": \"video/mp4; codecs=\\\"$AVC\\\"\", \"segs\": $(segs mse-h264-v)},"
		echo "   {\"mime\": \"audio/mp4; codecs=\\\"$AAC\\\"\", \"segs\": $(segs mse-h264-a)}]},"
		echo " \"mse.hevc\": {\"mse\": [{\"mime\": \"video/mp4; codecs=\\\"$HVC\\\"\", \"segs\": $(segs mse-hevc-v)},"
		echo "   {\"mime\": \"audio/mp4; codecs=\\\"$AAC\\\"\", \"segs\": $(segs mse-hevc-a)}]},"
		echo " \"mse.vp9\": {\"mse\": [{\"mime\": \"video/webm; codecs=\\\"vp9\\\"\", \"segs\": $(segs mse-vp9-v)}]},"
		echo " \"mse.vp9-opus\": {\"mse\": [{\"mime\": \"video/webm; codecs=\\\"vp9\\\"\", \"segs\": $(segs mse-vp9opus-v)},"
		echo "   {\"mime\": \"audio/webm; codecs=\\\"opus\\\"\", \"segs\": $(segs mse-vp9opus-a)}]},"
		echo " \"mse.av1\": {\"mse\": [{\"mime\": \"video/webm; codecs=\\\"$AV01\\\"\", \"segs\": $(segs mse-av1-v)},"
		echo "   {\"mime\": \"audio/webm; codecs=\\\"opus\\\"\", \"segs\": $(segs mse-av1-a)}]}"
		echo "};"
	} > cases.js
	exit 0
fi
OUT=${1:-$(cd "$(dirname "$0")/../../.." && pwd)/build/media-test}
mkdir -p "$OUT"
cd "$OUT"
V() { echo "-f lavfi -i testsrc2=size=$1:rate=$2"; }
A="-f lavfi -i sine=frequency=1000:beep_factor=4:sample_rate=48000 -ac 2"
f $(V 1280x720 30) $A -t 20 -c:v libx264 -profile:v main -pix_fmt yuv420p -g 60 -c:a aac -b:a 128k -shortest h264-aac-720p.mp4
f $(V 1920x1080 30) $A -t 20 -c:v libx264 -profile:v high -pix_fmt yuv420p -g 60 -b:v 6M -c:a aac -shortest h264-aac-1080p.mp4
f $(V 1920x1080 30) $A -t 20 -c:v libx265 -x265-params log-level=error:keyint=60 -pix_fmt yuv420p -tag:v hvc1 -b:v 4M -c:a aac -shortest hevc-aac-1080p.mp4
f $(V 1920x1080 30) $A -t 20 -c:v libx265 -x265-params log-level=error:keyint=60 -pix_fmt yuv420p10le -tag:v hvc1 -b:v 4M -c:a aac -shortest hevc10-aac-1080p.mp4
f $(V 720x576 25) $A -t 20 -c:v mpeg2video -b:v 4M -c:a ac3 -shortest mpeg2-ac3.ts
f $(V 640x360 25) $A -t 20 -c:v mpeg4 -q:v 4 -c:a libmp3lame -shortest mpeg4-mp3.mp4
f $(V 640x360 25) -t 20 -c:v libvpx -b:v 1M vp8-only.webm
# one H.264 stream in TS whose size changes: 640x360, 1280x720, 640x360 (continuous timestamps)
f $(V 640x360 30) -t 6 -c:v libx264 -g 30 -f mpegts size-a.ts
f $(V 1280x720 30) -t 6 -c:v libx264 -g 30 -f mpegts size-b.ts
printf 'file size-a.ts\nfile size-b.ts\nfile size-a.ts\n' > size.txt
f -f concat -i size.txt -c copy -f mpegts h264-sizechange.ts
rm -f size-a.ts size-b.ts size.txt
f -f lavfi -i "color=black:s=640x360:r=30,drawbox=c=white:t=fill:enable='lt(mod(t\,1)\,0.03)'" \
	-f lavfi -i "aevalsrc='if(lt(mod(t\,1)\,0.05)\,0.5*sin(2*PI*1000*t)\,0)|if(lt(mod(t\,1)\,0.05)\,0.5*sin(2*PI*1000*t)\,0)':s=48000" \
	-t 20 -c:v libx264 -g 30 -pix_fmt yuv420p -c:a aac -shortest av-sync.mp4
ls -l "$OUT"
