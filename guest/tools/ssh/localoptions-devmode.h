// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Developer mode (guest/layers/devmode): appended to localoptions.h for
 * build/ssh/bin/dropbear-devmode, the server that runs inside the jailer's native_devmode jail
 * (/etc/jail_native_devmode.conf) in place of the Developer Mode app's OpenSSH sshd. Dropbear fixes
 * these at compile time, where sshd takes them from its sshd_config and ~/.ssh/environment. */
#undef SFTPSERVER_PATH
#undef DEFAULT_PATH
/* The app's sshd_config: "Subsystem sftp /opt/openssh/lib/openssh/sftp-server". The jail links
 * /opt/openssh to the app's binaries-armv71/opt/openssh; it has no /usr/libexec. */
#define SFTPSERVER_PATH "/opt/openssh/lib/openssh/sftp-server"
/* The PATH that start-devmode.sh writes into ~/.ssh/environment, without the TV-only
 * /mnt/bsppart/bsp/usr/bin. */
#define DEFAULT_PATH "/usr/sbin:/usr/bin:/sbin:/bin:/opt/devmode/usr/bin"
