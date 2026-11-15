# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The test guest's 64-bit ARM side (docs/dev/architecture/test-guest.md), as newer webOS firmware
# ships one beside its 32-bit userland (queue, spacebuns): an AArch64 glibc in /usr/lib64 with its
# loader /usr/lib64/ld-linux-aarch64.so.1 (and /usr/lib/ld-linux-aarch64.so.1 linking to it), and
# the check tg-arm64 linked against it. The glibc is Debian's (libc6-arm64-cross, the guest build's
# aarch64 cross compiler's own), not the vendored userland's. Staged as a tree of its own,
# stage/testguest-arm64, stripped and checked with the aarch64 tools, which the testguest layer
# takes as it is: kept apart so a guest whose kernel cannot run AArch64 natively can take it under
# qemu-aarch64 instead.
tg_arm64_cc = find_program(get_option('arm64_cross_compile') + 'gcc', native: true)
tg_arm64_strip = find_program(get_option('arm64_cross_compile') + 'strip', native: true)
tg_arm64_readelf = find_program(get_option('arm64_cross_compile') + 'readelf', native: true)
tg_arm64 = custom_target('tg-arm64',
  input: 'tg-arm64.c',
  output: 'tg-arm64',
  depfile: 'tg-arm64.d',
  command: [tg_arm64_cc, '-O2', '-Wall', '-Wextra'] + (get_option('werror') ? ['-Werror'] : []) + [
    '-Wl,--dynamic-linker=/usr/lib64/ld-linux-aarch64.so.1',
    # Debian's loader searches /lib/aarch64-linux-gnu, /usr/lib/aarch64-linux-gnu, /lib and
    # /usr/lib, not /usr/lib64
    '-Wl,-rpath,/usr/lib64',
    '-MD', '-MF', '@DEPFILE@', '-o', '@OUTPUT@', '@INPUT@'])
# glibc's run-time libraries (libpthread, libdl and librt are its compatibility stubs) and GCC's
# runtime, which glibc loads for unwinding (pthread_cancel)
tg_arm64_args = []
foreach lib : ['ld-linux-aarch64.so.1', 'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2',
               'librt.so.1', 'libresolv.so.2', 'libutil.so.1', 'libgcc_s.so.1']
  path = run_command(tg_arm64_cc, '-print-file-name=' + lib, check: true).stdout().strip()
  if not fs.is_file(path)
    error(lib + ': not found by ' + tg_arm64_cc.full_path() + ' (libc6-arm64-cross, libgcc-s1-arm64-cross)')
  endif
  tg_arm64_args += ['--file', path, 'usr/lib64/' + lib,
                    lib.startswith('ld-') ? '755' : '644']
endforeach
tg_arm64_args += ['--link', '../lib64/ld-linux-aarch64.so.1', 'usr/lib/ld-linux-aarch64.so.1',
                  '--file', tg_arm64, 'usr/bin/tg-arm64', '755']
tg_arm64_tree = meson.project_build_root() / 'stage/testguest-arm64'
tg_arm64_stage = custom_target('testguest-arm64',
  output: 'testguest-arm64.stamp',
  depfile: 'testguest-arm64.d',
  # held to the glibc shipped here (Debian 13's): readelf -V of libc.so.6 lists its own versions
  command: [python, stage_py, '--strip', tg_arm64_strip, '--readelf', tg_arm64_readelf,
            '--glibc-max', '2.41', '--out', tg_arm64_tree, '--stamp', '@OUTPUT@',
            '--depfile', '@DEPFILE@'] + tg_arm64_args,
  depends: tg_arm64)
