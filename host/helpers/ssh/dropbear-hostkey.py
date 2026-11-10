#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Convert an unencrypted OpenSSH ed25519 or RSA private key (ssh-keygen -t ed25519|rsa -N '') to
Dropbear's private key format, so a host key can be made on any build host with only ssh-keygen and
python3.

    dropbear-hostkey.py OPENSSH_KEY DROPBEAR_KEY

Dropbear's formats (src/signkey.c, src/ed25519.c, src/rsa.c):
  ed25519  string "ssh-ed25519", string seed||public
  RSA      string "ssh-rsa", mpint e, mpint n, mpint d, mpint p, mpint q
OpenSSH stores RSA as n, e, d, iqmp, p, q; the mpints are already in SSH wire form, so they are
reordered as they are.
"""

import base64
import os
import struct
import sys


def strings(buf):
    """Yield the SSH wire-format strings of buf in order."""
    pos = 0
    while pos < len(buf):
        (n,) = struct.unpack(">I", buf[pos : pos + 4])
        yield buf[pos + 4 : pos + 4 + n]
        pos += 4 + n


def main(src, dst):
    lines = open(src).read().split("\n")
    b64 = "".join(l for l in lines if l and not l.startswith("-----"))
    blob = base64.b64decode(b64)
    magic = b"openssh-key-v1\0"
    if not blob.startswith(magic):
        sys.exit(f"{src}: not an OpenSSH private key")
    rest = blob[len(magic) :]
    it = strings(rest)
    cipher, kdf, _kdfopts = next(it), next(it), next(it)
    if cipher != b"none" or kdf != b"none":
        sys.exit(f"{src}: encrypted keys are not supported")
    # after the three strings: uint32 key count, string public key, string private section
    pos = sum(4 + len(s) for s in (cipher, kdf, _kdfopts))
    (count,) = struct.unpack(">I", rest[pos : pos + 4])
    if count != 1:
        sys.exit(f"{src}: expected one key, found {count}")
    it = strings(rest[pos + 4 :])
    next(it)  # public key blob
    priv = next(it)
    check1, check2 = struct.unpack(">II", priv[:8])
    if check1 != check2:
        sys.exit(f"{src}: corrupt private section")
    it = strings(priv[8:])
    ktype = next(it)

    def wire(b):
        return struct.pack(">I", len(b)) + b

    if ktype == b"ssh-ed25519":
        pub, secret = next(it), next(it)
        if len(pub) != 32 or len(secret) != 64 or secret[32:] != pub:
            sys.exit(f"{src}: malformed ed25519 key")
        out = wire(ktype) + wire(secret)
    elif ktype == b"ssh-rsa":
        n, e, d, _iqmp, p, q = (next(it) for _ in range(6))
        out = wire(ktype) + b"".join(wire(x) for x in (e, n, d, p, q))
    else:
        sys.exit(f"{src}: key type {ktype.decode()} is not supported (ed25519 or RSA)")

    fd = os.open(dst, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
