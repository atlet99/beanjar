// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_PEER_H
#define VIEWER_PEER_H
#include <gio/gio.h>
/* The viewer's end of a socket pair handed to QEMU (RegisterListener, RegisterOutListener) as a
 * GDBusConnection with message processing delayed: register objects, then start it. Takes fd. */
GDBusConnection *peer_connect(int fd, GError **error);
/* As above, with cancellation covering authentication and connection setup too. */
GDBusConnection *peer_connect_cancellable(int fd, GCancellable *cancel, GError **error);
#endif
