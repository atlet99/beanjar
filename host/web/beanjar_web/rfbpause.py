# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The /rfb relay's RFB filter: the page's picture paused while its H.264 stream plays
(docs/dev/architecture/web-viewer.md, "No RFB picture under H.264").

The relay passes every byte through unchanged, except FramebufferUpdateRequest and
EnableContinuousUpdates while paused, and redundant incremental requests while it keeps continuous
updates on for a page that no longer knows they are (below). To withhold messages it follows the
client's side of the stream: the handshake (ProtocolVersion, the security type, ClientInit), then
each client-to-server message by its length. From the server's side it reads only the handshake,
for the desktop's size (ServerInit). A client it cannot follow (RFB 3.3, a security type other
than None, a message type it does not know) is relayed as it is from then on, never paused.

Pausing with continuous updates on (noVNC enables them with neatvnc): the relay disables them,
and neatvnc answers EndOfContinuousUpdates, after which noVNC asks for each update itself. On
resume the relay enables them again; noVNC, believing them off, sends an incremental request after
each update, which the relay drops (neatvnc ignores them, and 0.9.1 stalls on one). On resume it
also asks for the whole screen, non-incremental, so the page gets a full picture at once.

beanjar-vnc is told too, so that it stops feeding neatvnc frames no one is sent (docs/dev/architecture/
vnc.md, "Paused RFB clients"): on pause and on resume, first, the relay sends a ClientCutText of
exactly PAUSE or RESUME, which beanjar-vnc takes on its --local socket (`vnc pipe`'s) as a marker,
never as clipboard text. A pause during the handshake is marked once the handshake is done.

No I/O here: client() and server() take the bytes each way and return what to send on;
pause() and resume() return what to send to the server.
"""

import struct

FBUR, CUT_TEXT, ECU = 3, 6, 150
PAUSE, RESUME = b"beanjar:rfb-pause", b"beanjar:rfb-resume"
# fixed-length client-to-server messages (RFB 3.8, TigerVNC's and QEMU's extensions)
FIXED = {0: 20, 3: 10, 4: 8, 5: 6, 150: 10, 160: 20, 250: 4}


def fbur(incremental, x, y, w, h):
    return struct.pack(">BBHHHH", FBUR, incremental, x, y, w, h)


def cut_text(text):
    return struct.pack(">BxxxI", CUT_TEXT, len(text)) + text


def ecu(enable, x=0, y=0, w=0, h=0):
    return struct.pack(">BBHHHH", ECU, enable, x, y, w, h)


class RfbPause:
    def __init__(self):
        self.cbuf = b""
        self.cstate = "version"  # version, security, init, normal, transparent
        self.minor = None
        self.security = None
        self.sbuf = b""
        self.sstate = "version"  # version, security, result, init, done
        self.width = self.height = 0
        self.paused = False
        self.marked = False  # PAUSE sent to beanjar-vnc, RESUME not yet
        self.server_cu = False  # continuous updates on at the server, as last sent to it
        self.client_cu = False  # the client asked for them
        self.cu_rect = (0, 0, 0, 0)
        self.withheld = 0  # requests withheld while paused (the tests)
        self.dropped = 0  # redundant incremental requests dropped

    @property
    def following(self):
        return self.cstate != "transparent"

    # -- the client's bytes --
    def client(self, data):
        if self.cstate == "transparent":
            return data
        self.cbuf += data
        out = []
        while True:
            n = self._client_length()
            if n is None or len(self.cbuf) < n:
                break
            msg, self.cbuf = self.cbuf[:n], self.cbuf[n:]
            out.append(self._client_message(msg))
            if self.cstate == "transparent":
                out.append(self.cbuf)
                self.cbuf = b""
                break
        return b"".join(out)

    def _client_length(self):
        b = self.cbuf
        if self.cstate == "version":
            return 12
        if self.cstate in ("security", "init"):
            return 1
        if not b:
            return None
        t = b[0]
        if t in FIXED:
            return FIXED[t]
        if t == 2:  # SetEncodings
            return None if len(b) < 4 else 4 + 4 * struct.unpack(">H", b[2:4])[0]
        if t == 6:  # ClientCutText; a negative length is the extended clipboard's
            return None if len(b) < 8 else 8 + abs(struct.unpack(">i", b[4:8])[0])
        if t == 248:  # ClientFence
            return None if len(b) < 9 else 9 + b[8]
        if t == 251:  # SetDesktopSize
            return None if len(b) < 8 else 8 + 16 * b[6]
        if t == 255:  # QEMU: 0 the extended key event, 1 audio
            if len(b) < 4:
                return None
            if b[1] == 0:
                return 12
            if b[1] == 1:
                op = struct.unpack(">H", b[2:4])[0]
                return {0: 4, 1: 4, 2: 10}.get(op, 0)
        return 0  # not known: from here on relayed as it is

    def _client_message(self, msg):
        if not msg:
            return self._transparent()
        if self.cstate == "version":
            try:
                self.minor = int(msg[8:11])
            except ValueError:
                self.minor = 0
            if not msg.startswith(b"RFB 003.") or self.minor < 7:
                return msg + self._transparent()
            self.cstate = "security"
            return msg
        if self.cstate == "security":
            self.security = msg[0]
            if self.security != 1:  # not None: what follows is the security type's
                return msg + self._transparent()
            self.cstate = "init"
            return msg
        if self.cstate == "init":
            self.cstate = "normal"
            return msg + (self._mark(PAUSE) if self.paused else b"")
        t = msg[0]
        if t == FBUR:
            incremental, x, y, w, h = struct.unpack(">xBHHHH", msg)
            if not incremental and x == 0 and y == 0:
                self.width, self.height = w, h
            if self.paused:
                self.withheld += 1
                return b""
            if incremental and self.server_cu:
                self.dropped += 1
                return b""
            return msg
        if t == ECU:
            enable, x, y, w, h = struct.unpack(">xBHHHH", msg)
            self.client_cu = bool(enable)
            if enable:
                self.cu_rect = (x, y, w, h)
            if self.paused:
                self.withheld += 1
                return b""
            self.server_cu = bool(enable)
            return msg
        return msg

    def _transparent(self):
        """From here on everything as it is: a paused picture resumed first."""
        out = self.resume() if self.paused and self.cstate == "normal" else b""
        self.paused = False
        self.cstate = "transparent"
        return out

    # -- the server's bytes: the handshake only, for the desktop's size --
    def server(self, data):
        if self.sstate == "done":
            return data
        self.sbuf += data
        while self.sstate != "done":
            b = self.sbuf
            if self.sstate == "version":
                n = 12
            elif self.sstate == "security":
                if self.minor is None or not b:
                    break
                n = 1 + b[0]
            elif self.sstate == "result":
                n = 4
            else:  # ServerInit
                n = None if len(b) < 24 else 24 + struct.unpack(">I", b[20:24])[0]
            if n is None or len(b) < n:
                break
            msg, self.sbuf = b[:n], b[n:]
            if self.cstate == "transparent":
                self.sstate = "done"
            elif self.sstate == "version":
                self.sstate = "security"
            elif self.sstate == "security":  # no types: a failure; 3.7 has no SecurityResult
                self.sstate = "done" if not msg[0] else "result" if self.minor >= 8 else "init"
            elif self.sstate == "result":
                self.sstate = "init" if msg == b"\0\0\0\0" else "done"
            else:
                self.width, self.height = struct.unpack(">HH", msg[:4])
                self.sstate = "done"
        if self.sstate == "done":
            self.sbuf = b""
        return data

    # -- the pause --
    def _mark(self, text):
        """beanjar-vnc's marker, once each way."""
        if (text == PAUSE) == self.marked:
            return b""
        self.marked = text == PAUSE
        return cut_text(text)

    def pause(self):
        """What to send the server to stop its updates ("" if nothing)."""
        if self.paused or self.cstate == "transparent":
            return b""
        self.paused = True
        if self.cstate != "normal":
            return b""
        out = self._mark(PAUSE)
        if self.server_cu:
            self.server_cu = False
            out += ecu(0)
        return out

    def resume(self):
        """What to send the server to start updates again, with the whole screen at once."""
        if not self.paused:
            return b""
        self.paused = False
        if self.cstate != "normal":
            return b""
        out = self._mark(RESUME)  # first: beanjar-vnc feeds neatvnc before the request below
        if self.width and self.height:  # first, so that the first update is the whole screen
            out += fbur(0, 0, 0, self.width, self.height)
        if self.client_cu and not self.server_cu:
            self.server_cu = True
            out += ecu(1, *self.cu_rect)
        return out
