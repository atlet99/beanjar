# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The TV picture: screenshot, elements, the viewer, beanjar-vnc and the screensaver."""

import argparse
import os
import sys
import tempfile

from .. import framing, keys, remote
from ..core import Library
from .common import Usage, emit, late_options, parse_size, progress, read_new_password, warn
from .forward import png_size, remote_picture


def cmd_viewer_open(lib, a):
    _late, rest = late_options(a, a.args, "beanjar viewer open")
    pid = lib.view(a.name, rest)
    return emit(a, {"name": a.name, "viewer": pid}, f"{a.name}: viewer pid {pid}")


def cmd_viewer_close(lib, a):
    closed = lib.unview(a.name)
    return emit(
        a,
        {"name": a.name, "closed": closed},
        f"{a.name}: " + ("viewer closed" if closed else "no viewer open"),
    )


def cmd_screenshot(lib, a):
    if a.last:
        if a.size:
            raise Usage("screenshot --last gives the kept thumbnail: it takes no --size")
        png = lib.last_thumbnail(a.name)
        if a.file == "-":
            if a.json:
                raise Usage("screenshot NAME - prints the PNG on stdout: it takes no --json")
            sys.stdout.buffer.write(png)
            sys.stdout.buffer.flush()
            return 0
        path = os.path.abspath(a.file)
        with open(path, "wb") as f:
            f.write(png)
        w, h = png_size(png)
        return emit(
            a,
            {"name": a.name, "file": path, "width": w, "height": h, "taken_by": "kept"}
            | framing.frame_geometry((w, h), framing.png_source(png)),
            path,
        )
    size = parse_size(a.size) if a.size else None
    if a.file == "-":
        # the PNG on stdout and nothing else there (what a remote screenshot reads): the
        # progress goes to stderr
        if a.json:
            raise Usage("screenshot NAME - prints the PNG on stdout: it takes no --json")
        with tempfile.TemporaryDirectory(prefix="beanjar-shot-") as tmp:
            path = os.path.join(tmp, "shot.png")
            lib.screenshot(a.name, path, size, out=progress)
            with open(path, "rb") as f:
                sys.stdout.buffer.write(f.read())
            sys.stdout.buffer.flush()
        return 0
    out = None if a.json else lambda line: print(line, file=sys.stderr, flush=True)
    path, (w, h), who = lib.screenshot(a.name, a.file, size, out=out)
    with open(path, "rb") as f:
        source = framing.png_source(f.read())
    return emit(
        a,
        {"name": a.name, "file": path, "width": w, "height": h, "taken_by": who}
        | framing.frame_geometry((w, h), source),
        path,
    )


def cmd_elements(_lib, a):
    from .. import elements

    if bool(a.name) == bool(a.image):
        raise Usage("elements takes NAME or --image PNG, exactly one")
    binary = elements.executable()
    if a.image:
        data = elements.read(a.image, binary, out=progress)
        with open(a.image, "rb") as f:
            source = framing.png_source(f.read())
    else:
        library = []

        def local_library():
            if not library:
                library.append(Library(a.library, a.repo))
            return library[0]

        host, name = remote.locate(a.name, lambda n: n in local_library().names(), warn)
        with tempfile.TemporaryDirectory(prefix="beanjar-elements-") as tmp:
            image = os.path.join(tmp, "screen.png")
            if host:
                rc, png = remote_picture(a, host, name)
                if rc:
                    return rc
                with open(image, "wb") as f:
                    f.write(png)
            else:
                local_library().screenshot(name, image, out=progress)
            data = elements.read(image, binary, out=progress)
            with open(image, "rb") as f:
                source = framing.png_source(f.read())
    # the display the picture shows (a beanjar screenshot records it), for callers' logs
    data |= framing.frame_geometry((data["width"], data["height"]), source)
    return emit(a, data, elements.snapshot(data))


def vnc_options(parser):
    parser.add_argument(
        "--listen",
        metavar="ADDRESS",
        help="link nat: listen here instead of the VM's own address "
        "(127.0.1.N), e.g. 0.0.0.0 or a LAN address of this host",
    )
    parser.add_argument(
        "--port",
        type=int,
        metavar="N",
        help="RFB port (default 5900; 0: any free port, as beanjar-web takes; "
        "link lan: this host's listener only, the LAN address keeps 5900)",
    )


def cmd_vnc_start(lib, a):
    # the manager's own options also after the name, among beanjar-vnc's
    late, rest = late_options(a, a.args, "beanjar vnc start", vnc_options)
    listen, port = late.listen or a.listen, a.port if late.port is None else late.port
    pid, info = lib.vnc(a.name, rest, listen=listen, port=port)
    data = {"name": a.name, "pid": pid, "vnc": info}
    if not info:
        return emit(
            a, data, f"{a.name}: beanjar-vnc pid {pid}, not listening yet (see its vnc.log)"
        )
    here = f" (on this host {info['local_url']})" if info.get("local_url") else ""
    return emit(
        a,
        data,
        f"{a.name}: beanjar-vnc pid {pid}, {info['url']}{here}, password in {info['password_file']}",
    )


def cmd_vnc_stop(lib, a):
    stopped = lib.unvnc(a.name)
    return emit(
        a,
        {"name": a.name, "stopped": stopped},
        f"{a.name}: " + ("beanjar-vnc stopped" if stopped else "no beanjar-vnc running"),
    )


def cmd_vnc_pipe(lib, a):
    from .. import pipe

    return pipe.vnc_pipe(lib.vm(a.name))


def cmd_vnc_audio_pipe(lib, a):
    from .. import pipe

    return pipe.audio_pipe(lib.vm(a.name))


def cmd_vnc_video_pipe(lib, a):
    from .. import pipe

    return pipe.video_pipe(lib.vm(a.name))


def cmd_screensaver(lib, a):
    from .. import screensaver

    data = screensaver.control(lib.vm(a.name), a.verb_group, a.timeout)
    status = "started" if a.verb_group == "start" else "stopped"
    text = f"{a.name}: screensaver {status}" + (" (unchanged)" if not data["changed"] else "")
    return emit(a, data, text)


def cmd_vnc_password(lib, a):
    """A VM's VNC password: where it is (made the first time), show it, set it, reset it. Only show
    prints the password itself."""
    verb = a.verb or None
    if a.file is not None and verb != "set":
        raise Usage(f"only beanjar vnc password {a.name} set takes a FILE")
    if verb in ("set", "reset"):
        password = None if verb == "reset" else read_new_password(a.file)
        vm = lib.set_vnc_password(a.name, password, out=progress)
        path = lib.vm(a.name).ensure_vnc_password()
        return emit(
            a,
            {"name": vm.name, "password_file": path},
            f"{vm.name}: {'a new random' if verb == 'reset' else 'the'} VNC password in "
            f"{path}; it takes effect at the next beanjar vnc start",
        )
    vm = lib.vm(a.name)
    path = vm.ensure_vnc_password()
    if verb == "show":
        with open(path) as f:
            password = f.readline().rstrip("\r\n")
        return emit(a, {"name": vm.name, "password": password}, password)
    return emit(
        a,
        {"name": vm.name, "password_file": path},
        f"{vm.name}: VNC password in {path} (any user name; "
        f"beanjar vnc password {vm.name} show prints it)",
    )


def register_pictures(cli):
    sub, command, vm_name = cli.sub, cli.command, cli.vm_name
    s = command(
        sub,
        "screenshot",
        "save the TV picture of a running VM (video windows and LG's cursor in) as a PNG, "
        "whether or not a viewer or beanjar-vnc is open; prints the path",
        cmd_screenshot,
        usage="beanjar screenshot NAME FILE [--size WxH | --last] [--json]",
    )
    vm_name(s, "screenshot")
    s.add_argument("file", metavar="FILE", help="the PNG to write; - for stdout, the PNG alone")
    s.add_argument(
        "--size",
        metavar="WxH",
        help="the picture's size, the TV picture letterboxed into it (default 1280x720); "
        "640x360 or smaller is a thumbnail, kept for --last",
    )
    s.add_argument(
        "--last",
        action="store_true",
        help="the last thumbnail kept, also of a VM that is off",
    )

    s = command(
        sub,
        "elements",
        "the TV picture's text and controls as a list (OCR and UI-element detection), "
        "for scripts and tests; requires the optional go-textshot CLI on this PC. "
        "Refs are valid for this frame only. go-textshot does not report focus on webOS "
        "today, so [focused] will rarely appear",
        cmd_elements,
        usage="beanjar elements [NAME | --image PNG] [--json]",
    )
    s.set_defaults(library_needed=False)
    s.add_argument("name", nargs="?", metavar="NAME", help="the VM: NAME or HOST/NAME")
    s.add_argument("--image", metavar="PNG", help="read an existing PNG here; no VM needed")


def register(cli):
    command, group, vm_name = cli.command, cli.group, cli.vm_name
    from .. import screensaver

    g = group("viewer", "the viewer window (display dbus)")
    s = command(g, "open", "open the viewer", cmd_viewer_open)
    vm_name(s, "viewer")
    s.add_argument("args", nargs=argparse.REMAINDER, help="viewer options, e.g. --remote")
    s = command(g, "close", "close the viewer", cmd_viewer_close)
    vm_name(s, "viewer")

    g = group("vnc", "the TV picture over VNC (beanjar-vnc)")
    s = command(
        g,
        "start",
        "serve the TV picture over VNC on the VM's address, port 5900, and with link lan "
        "also on its LAN address (display dbus; not with the viewer open); always behind the "
        "VM's own password (RSA-AES, any user name; beanjar vnc password)",
        cmd_vnc_start,
    )
    vnc_options(s)
    vm_name(s)
    s.add_argument(
        "args",
        nargs=argparse.REMAINDER,
        help="beanjar-vnc options, e.g. --size 1920x1080 --fps 60 --native-cursor",
    )
    s = command(g, "stop", "stop beanjar-vnc", cmd_vnc_stop)
    vm_name(s)
    s = command(
        g,
        "pipe",
        "relay stdin and stdout to the running beanjar-vnc's local RFB socket (run/rfb-local.sock, "
        "owner-only, no RFB authentication): an RFB client on stdio, e.g. the web viewer's; "
        "through ssh for HOST/NAME",
        cmd_vnc_pipe,
    )
    vm_name(s, "pipe")
    s = command(
        g,
        "audio-pipe",
        "relay stdin and stdout to the running beanjar-vnc's audio socket (run/audio-local.sock, "
        "owner-only): send 'opus' or 'pcm' and a newline, read a 16-byte header and the VM's "
        "mixed audio (48 kHz stereo; Opus packets, or s16le PCM), e.g. for the web viewer; "
        "through ssh for HOST/NAME. It ends at the end of stdin",
        cmd_vnc_audio_pipe,
    )
    vm_name(s, "pipe")
    s = command(
        g,
        "video-pipe",
        "relay stdin and stdout to the running beanjar-vnc's video socket (run/video-local.sock, "
        "owner-only): send 'h264' (optionally 'bitrate=N', kbit/s) and a newline, read a 32-byte "
        "header and the TV picture as H.264 access units, each with a 16-byte header, e.g. for "
        "the web viewer; through ssh for HOST/NAME. It ends at the end of stdin",
        cmd_vnc_video_pipe,
    )
    vm_name(s, "pipe")

    s = command(
        g,
        "password",
        "a VM's VNC password (identity/vnc.password, owner-only, random until set, kept "
        "across restarts; clients may give any user name). No verb: where it is; show: print "
        "it (alone, for copying); set: from FILE, one line, or from stdin (- or no FILE; a "
        f"prompt on a terminal), 1 to {keys.VNC_MAX} characters of printable ASCII, no space at "
        "either end; reset: a new random one. A running beanjar-vnc keeps the old one until it "
        "restarts",
        cmd_vnc_password,
        usage="beanjar vnc password NAME [show | set [FILE] | reset] [--json]",
    )
    vm_name(s, "password")
    s.add_argument(
        "verb",
        nargs="?",
        choices=("show", "set", "reset"),
        metavar="VERB",
        help="none (where it is), show, set or reset",
    )
    s.add_argument("file", nargs="?", metavar="FILE")

    g = group("screensaver", "start or stop the native webOS screensaver (verified on 10.3.1)")
    for verb in ("start", "stop"):
        s = command(
            g,
            verb,
            f"{verb} the screensaver and wait for native state and real windows",
            cmd_screensaver,
        )
        vm_name(s)
        s.add_argument(
            "--timeout",
            type=screensaver.timeout_value,
            default=screensaver.DEFAULT_TIMEOUT,
            metavar="SECONDS",
            help=f"native completion deadline, 5 to 300 seconds (default {screensaver.DEFAULT_TIMEOUT})",
        )
