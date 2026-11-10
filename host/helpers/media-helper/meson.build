# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Compressed decoding belongs to this per-VM helper, never to the viewer. The arm64 cross build
# (host_cross), which makes the packages, requires FFmpeg unless -Dffmpeg=disabled.
ffmpeg_opt = get_option('ffmpeg')
ffmpeg_req = host_cross and ffmpeg_opt.auto() ? true : ffmpeg_opt
ffmpeg_deps = []
foreach name : ['libavcodec', 'libavutil', 'libswresample']
  dep = dependency(name, required: ffmpeg_req)
  if dep.found()
    ffmpeg_deps += dep
  endif
endforeach
have_ffmpeg = ffmpeg_deps.length() == 3
if not have_ffmpeg
  ffmpeg_deps = []
endif
# The engine's files live in ../../media/decode/sources.txt (relative to that
# directory) so Meson and ad-hoc trial builds compile the same list.
fs = import('fs')
media_engine_sources = []
foreach s : fs.read('../../media/decode/sources.txt').split(' ')
  st = s.strip()
  if st != ''
    media_engine_sources += '../../media/decode/' + st
  endif
endforeach
media_decoder_sources = have_ffmpeg ? media_engine_sources : ['../../media/decode-unavailable.c', '../../media/pool.c']
# The VM's end of the guest's media ports (media-helper.c),
# published to build/media-helper/. The launcher starts it next to QEMU when it is there.
media_helper = executable('media-helper', ['media-helper.c', '../../media/ipc.c', '../../media/ivserver.c'] + media_decoder_sources + coverage_c,
  dependencies: [dependency('threads')] + ffmpeg_deps,
  override_options: ['c_std=gnu11', 'warning_level=2', 'werror=true'],
  build_by_default: true)
published += [['file', media_helper.full_path(), 'media-helper/media-helper']]
publish_depends += media_helper
