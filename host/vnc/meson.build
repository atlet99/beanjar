# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# beanjar-vnc (docs/dev/architecture/vnc.md): a VM's TV picture as a VNC server over the viewer's core
# (host/viewer), published to build/vnc/ and packaged as beanjar-vnc. It needs neatvnc and aml
# 1.0 and libopus (beanjar-build-deps-host; neatvnc 1.0 on trixie is a local build,
# docs/dev/guides/neatvnc-local.md); without them it is not built, except in the arm64 cross build
# (host_cross), which makes the packages: there they, and the video libraries, are required.
neatvnc_dep = dependency('neatvnc', version: '>= 1.0', required: host_cross)
aml_dep = dependency('aml1', required: host_cross)          # aml 1.x ships aml1.pc
opus_dep = dependency('opus', required: host_cross)          # --audio-local's Opus (libopus-dev)
have_vnc = neatvnc_dep.found() and aml_dep.found() and opus_dep.found()
# --video-local's H.264 (libavcodec-dev, libswscale-dev; swscale converts RGB to YUV where the GPU
# cannot, yuv.c): optional, without them no video socket
video_deps = [dependency('libavcodec', required: host_cross), dependency('libavutil', required: host_cross),
              dependency('libswscale', required: host_cross)]
have_vnc_video = true
foreach d : video_deps
  have_vnc_video = have_vnc_video and d.found()
endforeach
# The GBM frames (frames.c: composed into dma-bufs, no read back): GBM and libdrm's headers;
# without them the frames are read back as before. Required in the arm64 cross build.
gbm_deps = [dependency('gbm', required: host_cross), dependency('libdrm', required: host_cross)]
have_vnc_gbm = gbm_deps[0].found() and gbm_deps[1].found()
if have_vnc
  vnc_sources = ['main.c', 'keysym.c', 'audio.c', 'v4l2.c', 'tilediff.c', 'thumb.c']
  vnc_args = []
  vnc_deps = [neatvnc_dep, aml_dep, opus_dep, dependency('pixman-1'), dependency('egl'),
              dependency('glib-2.0'), dependency('threads'), dependency('glesv2')]
  if have_vnc_gbm
    vnc_sources += ['frames.c']
    vnc_args += '-DHAVE_GBM'
    vnc_deps += gbm_deps
  endif
  if have_vnc_video
    vnc_sources += ['video.c', 'h264v4l2.c', 'yuv.c']
    vnc_args += '-DHAVE_VIDEO'
    vnc_deps += video_deps
  else
    warning('libavcodec, libavutil or libswscale not found: beanjar-vnc is built without --video-local')
  endif
  vnc = executable('beanjar-vnc', vnc_sources,
    link_with: viewer_core,
    c_args: vnc_args,
    dependencies: vnc_deps,
    build_rpath: '/usr/lib/beanjar/viewer',     # meson adds $ORIGIN/../viewer, build/vnc's too
    install_rpath: '/usr/lib/beanjar/viewer',
    override_options: ['c_std=c11', 'debug=true'],
    build_by_default: true)
  published += [['file', vnc.full_path(), 'vnc/beanjar-vnc']]
  publish_depends += vnc
else
  warning('neatvnc, aml or opus not found: beanjar-vnc is not built (apt install beanjar-build-deps-host)')
endif
summary({'beanjar-vnc': have_vnc, 'beanjar-vnc video': have_vnc and have_vnc_video,
         'beanjar-vnc GBM frames': have_vnc and have_vnc_gbm},
        section: 'Host')
