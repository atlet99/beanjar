# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The guest's SSH programs (Dropbear: dropbear, dbclient, dropbearkey, scp; OpenSSH's
# sftp-server) as dynamic 32-bit ARM binaries against the firmware's own glibc, so getpwnam and
# the rest of NSS work as on the TV. dropbear-devmode is the same server built for the developer
# mode jail (guest/layers/devmode): its sftp-server path and default PATH are the ones the jail has
# (localoptions-devmode.h), since Dropbear fixes both at compile time.
# The rootssh and devmode layers take them from here; they are also published as build/ssh/bin,
# so a rootssh layer can be assembled on a host that does not build them.
ssh_env = {
  'HOST': meson.get_external_property('triple'),
  'CC': ' '.join(cc.cmd_array()),
  'CFLAGS': ' '.join(get_option('c_args') + ['-O2']),
  'STRIP': strip.full_path(),
}
build_ssh = files('build-ssh.sh')
ssh_obj = meson.current_build_dir()
localoptions = files('localoptions.h')

dropbear = custom_target('dropbear',
  output: ['dropbear', 'dbclient', 'dropbearkey', 'scp'],
  depends: sources['dropbear']['stamp'],
  depend_files: [build_ssh, localoptions],
  env: ssh_env,
  command: [build_ssh, 'dropbear', sources['dropbear']['dir'], ssh_obj / 'dropbear-obj',
            '@OUTDIR@', localoptions, '--', 'dropbear', 'dbclient', 'dropbearkey', 'scp'],
  console: true)
dropbear_devmode = custom_target('dropbear-devmode',
  output: 'dropbear-devmode',
  depends: sources['dropbear']['stamp'],
  depend_files: [build_ssh, localoptions, files('localoptions-devmode.h')],
  env: ssh_env,
  command: [build_ssh, 'dropbear', sources['dropbear']['dir'], ssh_obj / 'dropbear-devmode-obj',
            '@OUTDIR@', localoptions, files('localoptions-devmode.h'), '--', 'dropbear=dropbear-devmode'],
  console: true)
sftp_server = custom_target('sftp-server',
  output: 'sftp-server',
  depends: sources['openssh']['stamp'],
  depend_files: build_ssh,
  env: ssh_env,
  command: [build_ssh, 'openssh', sources['openssh']['dir'], ssh_obj / 'openssh-obj', '@OUTDIR@',
            '--', 'sftp-server'],
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
