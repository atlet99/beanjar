# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The guest's SSH programs (Dropbear: dropbear, dbclient, dropbearkey, scp; OpenSSH's
# sftp-server) as dynamic 32-bit ARM binaries against the firmware's own glibc, so getpwnam and
# the rest of NSS work as on the TV. dropbear-devmode is the same server built for the developer
# mode jail (guest/layers/devmode): its sftp-server path and default PATH are the ones the jail has
# (localoptions-devmode.h), since Dropbear fixes both at compile time.
# The rootssh and devmode layers take them from here; they are also published as build/ssh/bin,
# so a rootssh layer can be assembled on a host that does not build them.
#
# They depend on the cross file only, not on the target: each is built once, in
# <shared_dir>/ssh/<key>/<program> (meson/shared.py; the key: the cross file's host, compiler, flags
# and strip, and the source trees), and each target's build copies the programs; a changed input
# (a tree's wrap key, an options header, the script, the compiler's version) builds there again.
ssh_env = {
  'HOST': meson.get_external_property('triple'),
  'CC': ' '.join(cc.cmd_array()),
  'CFLAGS': ' '.join(get_option('c_args') + ['-O2']),
  'STRIP': strip.full_path(),
}
build_ssh = files('build-ssh.sh')
ssh_obj = meson.current_build_dir()
localoptions = files('localoptions.h')
ssh_key_lines = ['source ' + sources['dropbear']['dir'], 'source ' + sources['openssh']['dir']]
foreach k, v : ssh_env
  ssh_key_lines += k + ' ' + v
endforeach
ssh_root = shared_dir / 'ssh' / run_command(python, shared_py, 'key', ssh_key_lines,
                                            check: true).stdout().strip()
ssh_shared = [python, shared_py, 'run', '--tool', cc.cmd_array()[-1]]

dropbear_inputs = [sources['dropbear']['dir'] / '.wrap-key', build_ssh, localoptions]
dropbear = custom_target('dropbear',
  output: ['dropbear', 'dbclient', 'dropbearkey', 'scp'],
  depfile: 'dropbear.d',
  depends: sources['dropbear']['stamp'],
  depend_files: [build_ssh, localoptions],
  env: ssh_env,
  command: ssh_shared + ['--dir', ssh_root / 'dropbear', '--depfile', '@DEPFILE@',
            '--input', dropbear_inputs,
            '--file', ssh_root / 'dropbear/out/dropbear', '@OUTPUT0@',
            '--file', ssh_root / 'dropbear/out/dbclient', '@OUTPUT1@',
            '--file', ssh_root / 'dropbear/out/dropbearkey', '@OUTPUT2@',
            '--file', ssh_root / 'dropbear/out/scp', '@OUTPUT3@', '--',
            build_ssh, 'dropbear', sources['dropbear']['dir'], ssh_root / 'dropbear/obj',
            ssh_root / 'dropbear/out', localoptions, '--', 'dropbear', 'dbclient', 'dropbearkey', 'scp'],
  console: true)
dropbear_devmode = custom_target('dropbear-devmode',
  output: 'dropbear-devmode',
  depfile: 'dropbear-devmode.d',
  depends: sources['dropbear']['stamp'],
  depend_files: [build_ssh, localoptions, files('localoptions-devmode.h')],
  env: ssh_env,
  command: ssh_shared + ['--dir', ssh_root / 'dropbear-devmode', '--depfile', '@DEPFILE@',
            '--input', dropbear_inputs, files('localoptions-devmode.h'),
            '--file', ssh_root / 'dropbear-devmode/out/dropbear-devmode', '@OUTPUT@', '--',
            build_ssh, 'dropbear', sources['dropbear']['dir'], ssh_root / 'dropbear-devmode/obj',
            ssh_root / 'dropbear-devmode/out', localoptions, files('localoptions-devmode.h'), '--',
            'dropbear=dropbear-devmode'],
  console: true)
sftp_server = custom_target('sftp-server',
  output: 'sftp-server',
  depfile: 'sftp-server.d',
  depends: sources['openssh']['stamp'],
  depend_files: build_ssh,
  env: ssh_env,
  command: ssh_shared + ['--dir', ssh_root / 'openssh', '--depfile', '@DEPFILE@',
            '--input', sources['openssh']['dir'] / '.wrap-key', build_ssh,
            '--file', ssh_root / 'openssh/out/sftp-server', '@OUTPUT@', '--',
            build_ssh, 'openssh', sources['openssh']['dir'], ssh_root / 'openssh/obj',
            ssh_root / 'openssh/out', '--', 'sftp-server'],
  console: true)

# build/ssh/bin, checked against the firmware's glibc
ssh_bin = custom_target('ssh-bin',
  output: 'ssh-bin.stamp',
  depfile: 'ssh-bin.d',
  command: arm_stage + ['--out', ssh_obj / 'bin', '--stamp', '@OUTPUT@', '--depfile', '@DEPFILE@',
    '--file', dropbear[0], 'dropbear', '755', '--file', dropbear[1], 'dbclient', '755',
    '--file', dropbear[2], 'dropbearkey', '755', '--file', dropbear[3], 'scp', '755',
    '--file', dropbear_devmode, 'dropbear-devmode', '755', '--file', sftp_server, 'sftp-server', '755'],
  build_by_default: true)
ssh_bin_dir = ssh_obj / 'bin'
published += [['tree', ssh_bin_dir, 'ssh/bin']]
publish_depends += ssh_bin
