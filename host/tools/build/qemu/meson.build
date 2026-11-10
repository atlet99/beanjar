# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The host's own QEMU (build.sh explains it), published as build/qemu. The launcher runs it for
# every VM when it is there (paths.Tree.qemu), and the system QEMU otherwise. Its patches are needed
# by a TCG VM's virgl display on "surfaceless", a host without a DRM render node (WSL2), and by every
# host for the D-Bus display (patch 0004: a closed listener is dropped; Debian's QEMU 10.0 keeps
# sending it each frame). So `qemu` auto builds it wherever its build dependencies are found (role
# `host`, packaging/debian/build-deps), and requires them where `host/scripts/vm-launcher config
# RENDERNODE` (qemu.tcg_rendernode, the same code as the launcher's) says surfaceless. RENDERNODE in
# the environment of the configure counts as it does at launch. Without them the system QEMU is checked.
# The arm64 cross build (host_cross, meson/cross/debian-arm64.ini) builds it for the arm64
# packages: the render node is the target's, not this machine's, so auto requires it there, and
# no system QEMU of this machine is checked.
qemu_opt = get_option('qemu')
if host_cross
  qemu_req = qemu_opt.auto() ? true : qemu_opt
  qemu_why = 'cross build for ' + host_machine.cpu_family()
else
  rendernode = run_command(files('../../../scripts/vm-launcher'), 'config', 'RENDERNODE', check: true).stdout().strip()
  qemu_req = rendernode == 'surfaceless' and qemu_opt.auto() ? true : qemu_opt
  qemu_why = rendernode == 'surfaceless' ? 'no render node' : 'render node ' + rendernode
endif
qemu_missing = []
if not qemu_opt.disabled()
  # Debian 13's libraries (build.sh); QEMU's configure finds them by pkg-config, and its venv
  # takes the system Meson and pycotap.
  foreach d : [['virglrenderer', '>= 1.0'], ['epoxy', '>= 0'], ['gbm', '>= 0'], ['slirp', '>= 0'],
               ['pixman-1', '>= 0'], ['glib-2.0', '>= 0'], ['libpng', '>= 0'], ['zlib', '>= 0'],
               ['libusbredirparser-0.5', '>= 0.6'], ['libusb-1.0', '>= 1.0.13']]
    if not dependency(d[0], version: d[1], required: qemu_req).found()
      qemu_missing += [d[0]]
    endif
  endforeach
  # libfdt-dev has no pkg-config file; QEMU's configure looks for the library as this does.
  if not meson.get_compiler('c').find_library('fdt', has_headers: ['libfdt.h'], required: qemu_req).found()
    qemu_missing += ['libfdt']
  endif
  if not import('python').find_installation('python3', modules: ['pycotap'], required: qemu_req).found()
    qemu_missing += ['pycotap']
  endif
endif
if not qemu_opt.disabled() and qemu_missing.length() == 0
  qemu_stage = meson.current_build_dir() / 'stage'
  # A cross build hands build.sh what the cross file says: configure's cross prefix (property
  # cross_prefix), the host machine's pkg-config, and qemu-user for that CPU, which runs the staged
  # binary's --version check (qemu-user, beanjar-build-deps-host-arm64).
  qemu_cross = []
  if host_cross
    qemu_cross = [meson.get_external_property('cross_prefix'), find_program('pkg-config'),
                  find_program('qemu-' + host_machine.cpu_family(), native: true)]
  endif
  qemu = custom_target('qemu',
    output: 'qemu.stamp',
    depends: sources['qemu']['stamp'],
    depend_files: files('build.sh'),
    command: [files('build.sh'), sources['qemu']['dir'], meson.current_build_dir() / 'obj', qemu_stage,
              '@OUTPUT@'] + qemu_cross,
    console: true,
    build_by_default: true)
  published += [['tree', qemu_stage, 'qemu']]
  publish_depends += qemu
  summary({'QEMU': 'built, published to ' + publish_dir / 'qemu' + ' (' + qemu_why + ')'},
          section: 'Host')
else
  # The system QEMU, with what the launcher asks of it: the egl-headless and D-Bus displays, D-Bus
  # audio and virtio-gpu-gl (Debian: qemu-system-arm + qemu-system-modules-opengl), and the other
  # virtio devices. Only running a VM needs it, so a missing or incomplete one is a warning.
  sys_qemu = find_program('qemu-system-aarch64', required: false)
  missing = []
  if host_cross
    sys_qemu_state = 'not checked (cross build: the target\'s)'
  elif sys_qemu.found()
    displays = run_command(sys_qemu, '-display', 'help', check: false).stdout().split('\n')
    audiodevs = run_command(sys_qemu, '-audiodev', 'help', check: false).stdout()
    devices = run_command(sys_qemu, '-device', 'help', check: false).stdout()
    foreach d : ['egl-headless', 'dbus']
      if not displays.contains(d)
        missing += ['-display ' + d]
      endif
    endforeach
    if not audiodevs.split('\n').contains('dbus')
      missing += ['-audiodev dbus']
    endif
    foreach d : ['virtio-gpu-gl-pci', 'virtio-sound-pci', 'virtio-9p-pci', 'virtio-net-pci', 'virtio-blk-pci']
      if not devices.contains('name "' + d + '"')
        missing += ['-device ' + d]
      endif
    endforeach
    sys_qemu_state = sys_qemu.full_path() + ' ' + sys_qemu.version()
    if missing.length() > 0
      warning(sys_qemu.full_path() + ' lacks ' + ', '.join(missing) +
              ', which the launcher uses (Debian: apt install qemu-system-arm qemu-system-modules-opengl)')
      sys_qemu_state += ', lacks ' + ', '.join(missing)
    else
      sys_qemu_state += ', has what the launcher uses'
    endif
  else
    warning('no qemu-system-aarch64 in PATH, and QEMU is not built here: ' +
            'the launcher needs one (Debian: apt install qemu-system-arm qemu-system-modules-opengl)')
    sys_qemu_state = 'not found'
  endif
  qemu_summary = {
    'QEMU': 'not built (' + (qemu_opt.disabled() ? '-Dqemu=disabled' : 'lacks ' + ', '.join(qemu_missing)) +
            '), system QEMU',
    'System QEMU': sys_qemu_state,
  }
  # publish.py never removes a tree: one left from an earlier configure would stay, no longer
  # updated, and the launcher prefers it to the system QEMU checked above. So every build removes it.
  qemu_cleanup = custom_target('qemu-remove-published',
    output: 'qemu-remove-published.stamp',
    command: [find_program('remove-published.py'), publish_dir / 'qemu', '@OUTPUT@'],
    build_always_stale: true,
    build_by_default: true)
  if fs.exists(publish_dir / 'qemu')
    qemu_summary += {'build/qemu': publish_dir / 'qemu' + ' is left from an earlier build; ' +
                     'it is removed at build time, so the launcher runs the system QEMU'}
  endif
  summary(qemu_summary, section: 'Host')
endif
