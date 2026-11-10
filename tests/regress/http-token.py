#!/usr/bin/env python3
# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The network step's server on the VM host (scripts/regress --tv): HTTP on 127.0.0.1, which QEMU's
slirp (mode=direct) shows the guest as 10.0.2.2. It answers GET /<token> with the token and 404
otherwise, logs each request to stdout, and ends after SECONDS (it never outlives the step):

  tests/regress/http-token.py PORTFILE TOKEN [SECONDS]

The port is the kernel's choice (port 0), written to PORTFILE once the socket listens, so targets
running at once each have their own."""

import http.server
import os
import sys
import threading


def main(argv):
    portfile, token = argv[1], argv[2]
    seconds = float(argv[3]) if len(argv) > 3 else 600

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            ok = self.path == "/" + token
            body = (token + "\n").encode() if ok else b"not found\n"
            self.send_response(200 if ok else 404)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt, *args):
            print(f"REQUEST {self.client_address[0]} {fmt % args}", flush=True)

    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tmp = portfile + ".tmp"
    with open(tmp, "w") as f:
        f.write(f"{srv.server_address[1]}\n")
    os.replace(tmp, portfile)
    threading.Timer(seconds, srv.shutdown).start()
    srv.serve_forever()
    srv.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
