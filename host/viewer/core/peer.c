// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
/* QEMU is the authentication server on a listener's peer connection, and on Unix it offers only
 * EXTERNAL and DBUS_COOKIE_SHA1 there. GDBus's client claims its own uid (1000) for both, which
 * fails when QEMU runs in a rootless user namespace (mode=mitm): there it is uid 0 and sees the
 * socket's credentials as uid 0 too. So the SASL exchange is done here, with EXTERNAL and an
 * empty authorization identity ("whoever the credentials say"), which QEMU's GDBus accepts in and
 * out of a namespace; no QEMU patch is needed. Servers that reject that get the classic uid
 * claim, or ANONYMOUS where offered (QEMU's Windows builds). GDBus then gets the authenticated
 * stream with no authentication flag. */
#include "peer.h"
#include <gio/gunixconnection.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static gboolean say(GIOStream *stream, const char *line, GCancellable *cancel, GError **error)
{
    char buf[128];
    int n = snprintf(buf, sizeof buf, "%s\r\n", line);
    return g_output_stream_write_all(g_io_stream_get_output_stream(stream), buf, (gsize)n, NULL,
                                     cancel, error);
}
/* One reply line, read a byte at a time so nothing past it is taken from GDBus. */
static gboolean hear(GIOStream *stream, char *line, size_t size, GCancellable *cancel,
                     GError **error)
{
    GInputStream *in = g_io_stream_get_input_stream(stream);
    size_t n = 0;
    for (;;) {
        char c;
        gssize got = g_input_stream_read(in, &c, 1, cancel, error);
        if (got < 0)
            return FALSE;
        if (got == 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED, "peer closed during auth");
            return FALSE;
        }
        if (c == '\n')
            break;
        if (n + 1 < size)
            line[n++] = c;
    }
    if (n && line[n - 1] == '\r')
        n--;
    line[n] = 0;
    return TRUE;
}
/* Sends AUTH with the given mechanism and initial response (NULL: none); answers DATA with an
 * empty DATA. Returns TRUE on OK; on REJECTED leaves the reply in line and returns FALSE. */
static gboolean auth(GIOStream *stream, const char *mech, const char *initial, char *line,
                     size_t size, GCancellable *cancel, GError **error)
{
    char cmd[96];
    snprintf(cmd, sizeof cmd, "AUTH %s%s%s", mech, initial ? " " : "", initial ? initial : "");
    if (!say(stream, cmd, cancel, error) || !hear(stream, line, size, cancel, error))
        return FALSE;
    if (!strcmp(line, "DATA") &&
        (!say(stream, "DATA", cancel, error) || !hear(stream, line, size, cancel, error)))
        return FALSE;
    return !strncmp(line, "OK ", 3);
}
static void hex(char *out, const char *s)
{
    for (; *s; s++, out += 2)
        sprintf(out, "%02x", (unsigned char)*s);
    *out = 0;
}
GDBusConnection *peer_connect(int fd, GError **error)
{
    return peer_connect_cancellable(fd, NULL, error);
}
GDBusConnection *peer_connect_cancellable(int fd, GCancellable *cancel, GError **error)
{
    GSocket *socket = g_socket_new_from_fd(fd, error);
    if (!socket) {
        close(fd);
        return NULL;
    }
    GSocketConnection *stream = g_socket_connection_factory_create_connection(socket);
    g_object_unref(socket);
    GIOStream *io = G_IO_STREAM(stream);
    GDBusConnection *conn = NULL;
    char line[256], uid[16], id[40];
    /* The protocol's leading NUL byte, carrying SCM_CREDENTIALS as GDBus's client sends it. */
    if (!g_unix_connection_send_credentials(G_UNIX_CONNECTION(stream), cancel, error))
        goto out;
    gboolean ok = auth(io, "EXTERNAL", NULL, line, sizeof line, cancel, error);
    if (!ok && error && *error)
        goto out;
    if (!ok) {
        snprintf(uid, sizeof uid, "%u", (unsigned)getuid());
        hex(id, uid);
        ok = auth(io, "EXTERNAL", id, line, sizeof line, cancel, error);
        if (!ok && error && *error)
            goto out;
    }
    if (!ok && strstr(line, "ANONYMOUS")) {
        hex(id, "beanjar-viewer");
        ok = auth(io, "ANONYMOUS", id, line, sizeof line, cancel, error);
        if (!ok && error && *error)
            goto out;
    }
    if (!ok) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "peer auth failed: %s", line);
        goto out;
    }
    /* Dmabuf scanouts and ScanoutMap surfaces arrive as file descriptors. */
    if (!say(io, "NEGOTIATE_UNIX_FD", cancel, error) || !hear(io, line, sizeof line, cancel, error))
        goto out;
    if (strcmp(line, "AGREE_UNIX_FD")) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "peer refused fds: %s", line);
        goto out;
    }
    if (!say(io, "BEGIN", cancel, error))
        goto out;
    conn = g_dbus_connection_new_sync(io, NULL, G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING,
                                      NULL, cancel, error);
out:
    g_object_unref(stream);
    return conn;
}
