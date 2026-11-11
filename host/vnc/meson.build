# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# beanjar-vnc (docs/dev/architecture/vnc.md): a VM's TV picture as a VNC server over the viewer's core
# (host/viewer), published to build/vnc/ and packaged as beanjar-vnc. It is built on neatvnc 1.0 and
# aml 1.0 and the private FFmpeg, the prebuilt copies of packaging/vendor.toml (meson/meson.build),
# and needs libopus (beanjar-build-deps-host); without libopus it is not built, except in the arm64
# cross build (host_cross), which makes the packages: there it, and GBM, are required.
opus_dep = dependency('opus', required: host_cross)          # --audio-local's Opus (libopus-dev)
have_vnc = opus_dep.found()
# The private FFmpeg (one process never loads two FFmpeg copies, whose symbol versions are the
# same): --video-local's H.264 (libavcodec, libavutil, libswscale; swscale converts RGB to YUV
# where the GPU cannot, yuv.c).
video_deps = [vendor_deps['libavcodec'], vendor_deps['libavutil'], vendor_deps['libswscale']]
# libneatvnc's own FFmpeg libraries, libavfilter among them, which this program does not call.
# libneatvnc's RUNPATH is $ORIGIN, its own directory, so the dynamic linker would take them from
# the system path, Debian's, unless this program needs them itself and its RUNPATH finds them in
# the private copy: they are NEEDED here, past the --as-needed that Debian's GCC links with (one
# -Wl argument, which Meson keeps in one piece and in place).
neatvnc_ffmpeg = declare_dependency(link_args: ['-L' + vendor['ffmpeg'] / 'lib',
  '-Wl,--push-state,--no-as-needed,-lavcodec,-lavfilter,-lavutil,--pop-state'])
# The GBM frames (frames.c: composed into dma-bufs, no read back): GBM and libdrm's headers;
# without them the frames are read back as before. Required in the arm64 cross build.
gbm_deps = [dependency('gbm', required: host_cross), dependency('libdrm', required: host_cross)]
have_vnc_gbm = gbm_deps[0].found() and gbm_deps[1].found()
if have_vnc
  vnc_sources = ['main.c', 'keysym.c', 'audio.c', 'v4l2.c', 'tilediff.c', 'thumb.c', 'rate.c',
                 'video.c', 'h264v4l2.c', 'yuv.c']
  vnc_args = ['-DHAVE_VIDEO']
  vnc_deps = video_deps + [neatvnc_ffmpeg, vendor_deps['neatvnc'], vendor_deps['aml1'], opus_dep,
              dependency('pixman-1'), dependency('egl'), dependency('glib-2.0'),
              dependency('threads'), dependency('glesv2')]
  if have_vnc_gbm
    vnc_sources += ['frames.c']
    vnc_args += '-DHAVE_GBM'
    vnc_deps += gbm_deps
  endif
  vnc = executable('beanjar-vnc', vnc_sources,
    link_with: viewer_core,
    c_args: vnc_args,
    dependencies: vnc_deps,
    # meson adds $ORIGIN/../viewer, build/vnc's too; the private libraries beside build/vnc
    # (build/ffmpeg, build/neatvnc) and installed under /usr/lib/beanjar
    build_rpath: ':'.join(['/usr/lib/beanjar/viewer', vendor_rpath['neatvnc'], vendor_rpath['ffmpeg']]),
    override_options: ['c_std=c11', 'debug=true'],
    build_by_default: true)
  published += [['file', vnc.full_path(), 'vnc/beanjar-vnc']]
  publish_depends += vnc
else
  warning('opus not found: beanjar-vnc is not built (apt install beanjar-build-deps-host)')
endif
summary({'beanjar-vnc': have_vnc, 'beanjar-vnc GBM frames': have_vnc and have_vnc_gbm},
        section: 'Host')
