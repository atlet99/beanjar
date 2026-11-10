#!/bin/sh
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Developer mode (guest/layers/devmode): our stand-in for the Developer Mode app's start-devmode.sh.
#
# On a TV, /etc/init/devmode.conf runs start-devmode.sh from the LG store app com.palmdts.devmode
# (/media/cryptofs/apps/usr/palm/services/com.palmdts.devmode.service), which the emulator does
# not have. That script installs the app's OpenSSH where /etc/jail_native_devmode.conf mounts it,
# makes the developer key and runs sshd as "prisoner" in the native_devmode jail on port 9922.
# This script does the same with our own binaries and the VM's own keys, which the per-VM layer
# (the host's <vm>/identity) brings in; nothing is asked of LG's servers
# (the app's developer.lge.com session check is left out).
#
# devmode.override runs it when /var/luna/preferences/devmode_enabled exists. It execs the jailer,
# so the devmode job runs for as long as the server does.
set -u

SSH_PORT=9922
DEVMODE_SSHID=com.palm.devmode.openssh
DEVELOPER_HOME=/media/developer
SERVICE_DIR=/media/cryptofs/apps/usr/palm/services/com.palmdts.devmode.service
# jail_native_devmode.conf takes binaries-i686 only on qemux86 (LG's emulator); nyx reports
# goldfinger here, as on the TV.
DEVROOT=$SERVICE_DIR/binaries-armv71
CRYPTO_SSH=$DEVROOT/opt/openssh
HOST_KEY_DIR=/var/lib/devmode/sshd
SRC=/usr/libexec/emulator-devmode
# The per-VM layer's: developer_rsa (unencrypted) and .pub, ssh_host_rsa_key (Dropbear's format).
KEYS=/etc/emulator/devmode

# The "app": dropbear and sftp-server where the jail mounts the app's openssh (read-only, as
# /opt/openssh). The path is on the appstore state disk (or tmpfs), so it is refreshed from the
# layer on every start. binaries-armv71/opt/devmode stays empty, like the jail's /opt/devmode.
mkdir -p "$CRYPTO_SSH/sbin" "$CRYPTO_SSH/lib/openssh" "$DEVROOT/opt/devmode"
cp -f "$SRC/dropbear" "$CRYPTO_SSH/sbin/dropbear"
cp -f "$SRC/sftp-server" "$CRYPTO_SSH/lib/openssh/sftp-server"
chmod 755 "$CRYPTO_SSH/sbin/dropbear" "$CRYPTO_SSH/lib/openssh/sftp-server"

# The developer key pair, where the app keeps it; /var/luna/preferences/webos_rsa is the copy the
# key server hands out on port 9991. As on a TV the private key is encrypted with the device's
# passphrase: the first six characters of its NDUID, upper case, which is what
# com.palm.service.devmode/getPassphrase answers (it asks com.palm.systemservice/deviceInfo/query
# for the nduid, which is nyx's DeviceInfo). The encryption is the traditional PEM
# ("BEGIN RSA PRIVATE KEY", DEK-Info: AES-128-CBC), the format of the TV's
# ssh-keygen and what ares-novacom --getkey parses: OpenSSL 1.x writes it by
# default, 3.x needs -traditional (PKCS#8 "BEGIN ENCRYPTED PRIVATE KEY" fails
# the check). The encrypted key is kept across boots, so a developer's saved copy stays valid; it
# is made again only when it is missing or stale: when the VM's developer key (another identity) or
# the passphrase (another NDUID) changed. KEY.stamp records the two as one SHA-256 of the public
# key and the passphrase (never the passphrase itself); openssl dgst, since openssl is needed anyway.
# A new served copy replaces the old by a rename, as the key server may already be answering; a
# failed encryption leaves no key and no stamp.
KEY=/var/lib/devmode/ssh/webos_rsa
SERVED_KEY=/var/luna/preferences/webos_rsa
mkdir -p /var/lib/devmode/ssh
chmod 700 /var/lib/devmode/ssh
rm -f "$SERVED_KEY.new"
nduid=$(/usr/bin/nyx-cmd DeviceInfo query nduid 2>/dev/null | tr -d ' \r\n')
case $nduid in
[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*)
	pass=$(printf %s "$nduid" | cut -c1-6 | tr a-f A-F)
	stamp=$({ cat "$KEYS/developer_rsa.pub" && printf %s "$pass"; } | /usr/bin/openssl dgst -sha256 2>/dev/null) || stamp=
	if [ -n "$stamp" ] && [ -s "$KEY" ] && [ -s "$SERVED_KEY" ] && [ "$(cat "$KEY.stamp" 2>/dev/null)" = "$stamp" ]; then
		echo "devmode: developer key kept (same key and passphrase)"
	else
		rm -f "$KEY.stamp" "$KEY"
		umask 077
		# -traditional (OpenSSL 3.x) or plain (1.x writes it anyway); first success wins.
		if { P=$pass /usr/bin/openssl rsa -traditional -in "$KEYS/developer_rsa" -aes128 -passout env:P \
				-out "$KEY" 2>/dev/null || \
		   P=$pass /usr/bin/openssl rsa -in "$KEYS/developer_rsa" -aes128 -passout env:P \
				-out "$KEY" 2>/dev/null; } &&
		   cp -f "$KEY" "$SERVED_KEY.new" &&
		   chmod 644 "$SERVED_KEY.new" &&
		   mv -f "$SERVED_KEY.new" "$SERVED_KEY"
		then
			[ -z "$stamp" ] || printf '%s\n' "$stamp" >"$KEY.stamp"
			echo "devmode: developer key encrypted with the device's passphrase"
		else
			rm -f "$KEY" "$SERVED_KEY.new" "$SERVED_KEY"
			echo "devmode: openssl could not encrypt the developer key; the key server has none" >&2
		fi
		umask 022
	fi
	unset pass stamp
	;;
*)
	rm -f "$SERVED_KEY" "$KEY.stamp"
	echo "devmode: no NDUID from nyx ('$nduid'); the key server has no key" >&2 ;;
esac
unset nduid KEY SERVED_KEY
cp -f "$KEYS/developer_rsa.pub" /var/lib/devmode/ssh/webos_rsa.pub
chmod 644 /var/lib/devmode/ssh/webos_rsa.pub

# /media/developer: prisoner's home. Only log/ and temp/ are writable for prisoner (ares uploads
# to temp/); appinstalld puts apps in apps/. The app runs sshd with StrictModes=no and leaves the
# tree 775; Dropbear has no such switch and refuses an authorized_keys whose file or directories
# are group- or world-writable, so home and .ssh are 755 and the key file 644, owned by root.
mkdir -p "$DEVELOPER_HOME/log" "$DEVELOPER_HOME/temp" "$DEVELOPER_HOME/.ssh"
cp -f /var/lib/devmode/ssh/webos_rsa.pub "$DEVELOPER_HOME/.ssh/authorized_keys"
chown 0:0 "$DEVELOPER_HOME" "$DEVELOPER_HOME/.ssh" "$DEVELOPER_HOME/.ssh/authorized_keys"
chmod 755 "$DEVELOPER_HOME" "$DEVELOPER_HOME/.ssh"
chmod 644 "$DEVELOPER_HOME/.ssh/authorized_keys"
chmod 777 "$DEVELOPER_HOME/log" "$DEVELOPER_HOME/temp"

# The host key (RSA, the VM's own and stable across states), at the path the jail
# copies into itself (readable there, mode 644, since the server runs as prisoner).
mkdir -p "$HOST_KEY_DIR"
chmod 700 "$HOST_KEY_DIR"
cp -f "$KEYS/ssh_host_rsa_key" "$HOST_KEY_DIR/ssh_host_rsa_key"
chmod 600 "$HOST_KEY_DIR/ssh_host_rsa_key"
# The jail copies the host key only when it is built, and keeps it in /var/palm/jail across boots.
# Tear it down when the copy is stale (another host key), so the jailer builds it again.
JAILED_KEY=/var/palm/jail/$DEVMODE_SSHID$HOST_KEY_DIR/ssh_host_rsa_key
if [ -e "$JAILED_KEY" ] && ! cmp -s "$JAILED_KEY" "$HOST_KEY_DIR/ssh_host_rsa_key"; then
	echo "devmode: host key changed, rebuilding the jail"
	/usr/bin/jailer -D -i "$DEVMODE_SSHID" || true
fi

# The app's sshd options, in Dropbear's terms:
#   -D                            -F (foreground; the job tracks the process)
#   -h .../ssh_host_rsa_key       -r .../ssh_host_rsa_key (Dropbear's format)
#   PasswordAuthentication=no     -s (password auth is also compiled out)
#   PermitRootLogin=no            -w
#   PermitUserEnvironment=yes     -e: pass the jail's environment (the jailer's setenv lines) on
#     with ~/.ssh/environment     to sessions, which is what the app's environment file holds;
#                                 PATH is dropbear-devmode's compiled-in DEFAULT_PATH
#   ClientAliveInterval 15, CountMax 4  -K 15 (Dropbear gives up after 3 missed replies)
#   Subsystem sftp /opt/openssh/lib/openssh/sftp-server   compiled in (SFTPSERVER_PATH)
#   UseDNS=no, Compression no     Dropbear does neither (built without zlib)
#   StrictModes=no                none; see the permissions above
# -E logs to stderr, which the job does not keep (as on the TV); the pid file goes to the jail's
# /tmp, the only place prisoner can write it.
exec /usr/bin/jailer -t native_devmode -i "$DEVMODE_SSHID" -p "$DEVELOPER_HOME/" -s /bin/sh \
	/opt/openssh/sbin/dropbear -F -E -e -s -w -K 15 \
	-r "$HOST_KEY_DIR/ssh_host_rsa_key" -P /tmp/devmode-sshd.pid -p "$SSH_PORT"
