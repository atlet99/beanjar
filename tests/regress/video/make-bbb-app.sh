#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds org.webosbrew.beanjar.sitevideo, a web app that plays a 30 s clip of Big Buck Bunny
# (c) copyright 2008, Blender Foundation, www.bigbuckbunny.org, CC-BY 3.0, full screen in a loop,
# for trying video playback on a target. The app's files are in tests/regress/video/bbb-app/.
# The clip (720p H.264, no audio) and the icon (the film's logo) are not committed: this downloads
# Blender Foundation's 720p movie and press kit into OUT/dl (SHA-256 checked), cuts the clip
# without re-encoding (a stream copy from 76 s, the meadow scene), draws the icons from the logo's
# SVG, and runs ares-package. Needs curl, unzip, ffmpeg, rsvg-convert, magick and ares-package
# (the PC's ares-cli: . ~/.cargo/env).
#   tests/regress/video/make-bbb-app.sh [OUT]            OUT defaults to build/sitevideo; makes
#                                                        OUT/org.webosbrew.beanjar.sitevideo_1.0.0_all.ipk
#   beanjar ares-install -d VM OUT/org.webosbrew.beanjar.sitevideo_1.0.0_all.ipk
#   beanjar ares-launch -d VM org.webosbrew.beanjar.sitevideo
#   beanjar ares-install -d VM --remove org.webosbrew.beanjar.sitevideo
set -eu
TOP=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${1:-$TOP/build/sitevideo}
MOVIE_URL=https://download.blender.org/peach/bigbuckbunny_movies/big_buck_bunny_720p_h264.mov.zip
MOVIE_SHA256=b0c9ade80b086179feee41514929de583966eec87c703575d97f051f88b33b67
KIT_URL=https://download.blender.org/peach/presskit.zip
KIT_SHA256=70bef6a5eba71f84a3d41edac770a55e3549f0126613834a977b3ebb38c4d4f1
mkdir -p "$OUT/dl"
fetch() { # URL SHA256 FILE
	[ -f "$OUT/dl/$3" ] || curl -fsSL -o "$OUT/dl/$3" "$1"
	echo "$2  $OUT/dl/$3" | sha256sum -c --quiet - || { echo "$3: checksum mismatch" >&2; exit 1; }
}
fetch "$MOVIE_URL" "$MOVIE_SHA256" movie.zip
fetch "$KIT_URL" "$KIT_SHA256" presskit.zip
A="$OUT/app"
rm -rf "$A" "$OUT/src"; mkdir -p "$A" "$OUT/src"
cp "$TOP/tests/regress/video/bbb-app/appinfo.json" "$TOP/tests/regress/video/bbb-app/index.html" "$A/"
unzip -o -q "$OUT/dl/movie.zip" -d "$OUT/src"
unzip -o -q "$OUT/dl/presskit.zip" 'poster-presskit/logo-kleur/logo.svg' -d "$OUT/src"
ffmpeg -hide_banner -loglevel error -y -ss 76 -i "$OUT"/src/*.mov -t 30 -map 0:v:0 -c copy -an \
	-movflags +faststart "$A/bbb.mp4"
rsvg-convert -w 1000 "$OUT/src/poster-presskit/logo-kleur/logo.svg" -o "$OUT/src/logo.png"
for s in 80 130; do
	i=$((s * 92 / 100))
	magick "$OUT/src/logo.png" -filter Lanczos -resize ${i}x${i} -background none -gravity center \
		-extent ${s}x${s} -define png:color-type=6 "$A/icon-$s.png"
done
rm -f "$OUT"/*.ipk
ares-package "$A" -o "$OUT"
ls -l "$OUT"/*.ipk
