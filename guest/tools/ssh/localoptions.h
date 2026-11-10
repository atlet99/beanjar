// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* Dropbear build options for the webOS emulator (guest/tools/ssh/build-ssh.sh copies this to
 * the build's localoptions.h). The server is key-only: password logins are compiled out, so root's
 * empty password can never be used, whatever the command line says. */
#define DROPBEAR_SVR_PASSWORD_AUTH 0
#define DROPBEAR_SVR_PAM_AUTH      0
#define DROPBEAR_X11FWD            0
#define DO_MOTD                    0
/* OpenSSH's sftp-server from the same build; the "sftp" subsystem (and scp in its default sftp
 * mode) runs it. */
#define SFTPSERVER_PATH           "/usr/libexec/sftp-server"
#define DROPBEAR_PATH_SSH_PROGRAM "/usr/bin/dbclient"
