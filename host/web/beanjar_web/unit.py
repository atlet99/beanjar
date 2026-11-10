# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""beanjar-web start, enable, disable, stop and status: `beanjar-web serve` as the systemd user
unit beanjar-web (docs/dev/architecture/web-viewer.md, "Commands"). start makes it a transient unit,
gone at reboot; enable writes the same service as a unit file that starts at boot (with
lingering), and disable removes it.

start runs `systemd-run --user --unit beanjar-web --collect` with Restart=on-failure (not for exit
status 2: a bad option, a port taken, which a restart does not fix), the options made absolute, the
manager's command resolved to an absolute path now, and the environment the manager needs (PATH,
whose ~/.local/bin systemd's user environment may lack; the XDG directories; BEANJAR_*; the ssh
agent for other hosts). The unit's RuntimeDirectory holds state.json, written by serve once it
listens (its address, port, scheme, auth mode, --hosts and the secret's file, never the secret),
and is removed by systemd when the unit stops; start waits for it, status reads it. enable's unit
file ($XDG_CONFIG_HOME/systemd/user/UNIT.service) has the same command line, environment and
service properties, in systemd's own quoting, and WantedBy=default.target.
BEANJAR_WEB_UNIT names another unit (the tests'), so they never touch the user's.
"""

import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

from . import access

UP = ("active", "activating", "deactivating", "reloading")
UNIT_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,63}")
PASS_ENV = ("PATH", "LANG", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "SSH_AUTH_SOCK")
WAIT = 20.0


def fail(message):
    print(f"beanjar-web: {message}", file=sys.stderr)
    return 1


def unit_name():
    name = os.environ.get("BEANJAR_WEB_UNIT") or "beanjar-web"
    if not UNIT_RE.fullmatch(name):
        sys.exit(f"beanjar-web: BEANJAR_WEB_UNIT={name!r} is not a unit name")
    return name


def systemctl(*words):
    return subprocess.run(["systemctl", "--user", *words], capture_output=True, text=True)


def have_systemd():
    return shutil.which("systemd-run") and systemctl("show-environment").returncode == 0


def linger():
    """Have logind linger the user, so the unit stays after the user's last session ends (a user
    may set their own); a warning if it cannot. BEANJAR_LINGER=0 (the tests): leave it."""
    if os.environ.get("BEANJAR_LINGER", "1") == "0":
        return
    uid = str(os.getuid())
    try:
        r = subprocess.run(["loginctl", "show-user", uid, "--property=Linger", "--value"],
                           capture_output=True, text=True, timeout=5)  # fmt: skip
        if r.stdout.strip() == "yes":
            return
        r = subprocess.run(["loginctl", "enable-linger", uid], capture_output=True, text=True,
                           timeout=10)  # fmt: skip
        why = r.stderr.strip() if r.returncode else None
    except (OSError, subprocess.TimeoutExpired) as e:
        why = str(e)
    if why is not None:
        print(f"beanjar-web: cannot enable lingering ({why}): the unit stops when your last "
              "session ends", file=sys.stderr)  # fmt: skip


def props(unit):
    r = systemctl("show", f"{unit}.service", "-p", "ActiveState", "-p", "MainPID")
    out = dict(line.split("=", 1) for line in r.stdout.splitlines() if "=" in line)
    return out.get("ActiveState", "inactive"), int(out.get("MainPID") or 0)


def state_path(unit):
    run = os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"
    return os.path.join(run, unit, "state.json")


def read_state(unit):
    try:
        with open(state_path(unit)) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def describe(state):
    """status's lines for a running server's state.json."""
    acc = access.Access("none")
    acc.mode = state["auth"]
    if acc.mode == "token":
        acc.secret = access.read_secret(state["file"]) or "?"
    every = access.url_host(state["listen"])[1]
    on = f"every address ({state['listen']})" if every else state["listen"]
    lines = [
        f"listening on {on}, port {state['port']}, {state['scheme']}, --auth {acc.mode}",
        f"open {access.url(state['scheme'], state['listen'], state['port'], acc)}",
    ]
    if acc.mode == "password":
        lines.append(f"the password is in {state['file']}")
    if state.get("hosts"):
        lines.append(f"only the VMs of --hosts {','.join(state['hosts'])}")
    return lines


def serve_words(a, beanjar, state_file):
    """serve's options for the unit: a's, every path absolute."""
    absolute = os.path.abspath
    words = ["--listen", a.listen, "--port", str(a.port), "--auth", a.auth]
    if a.auth != "none":
        words += ["--token-file", absolute(a.token_file or access.default_token_file())]
    if a.password_file:
        words += ["--password-file", absolute(a.password_file)]
    if a.certificate:
        words += ["--certificate", absolute(a.certificate), "--key", absolute(a.key)]
    if a.size:
        words += ["--size", a.size]
    if a.fps:
        words += ["--fps", str(a.fps)]
    if a.hosts:
        words += ["--hosts", ",".join(a.hosts)]
    words += ["--novnc", absolute(a.novnc), "--icons", absolute(a.icons)]
    return words + ["--beanjar", shlex.join(beanjar), "--state-file", state_file]


DESCRIPTION = "beanjar-web: Beanjar's VMs in a browser"


def unit_path(unit):
    """enable's unit file: where systemd looks for the user's own units."""
    config = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(config, "systemd", "user", f"{unit}.service")


def persistent(unit):
    return os.path.isfile(unit_path(unit))


def enabled(unit):
    return systemctl("is-enabled", f"{unit}.service").stdout.strip()


def systemd_quote(word, exec_line=False):
    """word as one double-quoted word of a unit file: backslash and quote escaped, % doubled (a
    specifier otherwise), and in ExecStart= $ doubled too (a variable otherwise)."""
    if any(ord(c) < 32 or ord(c) == 127 for c in word):
        raise ValueError(f"a control character in {word!r}")
    word = word.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%")
    return '"' + (word.replace("$", "$$") if exec_line else word) + '"'


def service(a, beanjar_command, unit):
    """What start and enable run: (properties, environment, command line), or an error string."""
    beanjar = shlex.split(beanjar_command)
    if not beanjar:
        return "--beanjar is empty"
    if "/" not in beanjar[0]:
        found = shutil.which(beanjar[0])
        if not found:
            return f"the manager's command {beanjar[0]!r} is not on PATH (--beanjar COMMAND)"
        beanjar[0] = found
    package_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    env = {k: os.environ[k] for k in PASS_ENV if os.environ.get(k)}
    env.update({k: v for k, v in os.environ.items() if k.startswith("BEANJAR_")})
    env.pop("BEANJAR_WEB_UNIT", None)
    env["PYTHONPATH"] = package_dir
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    properties = [("Restart", "on-failure"), ("RestartSec", "2"), ("RestartPreventExitStatus", "2"),
                  ("RuntimeDirectory", unit), ("RuntimeDirectoryMode", "0700")]  # fmt: skip
    argv = [sys.executable, "-m", "beanjar_web", "serve",
            *serve_words(a, beanjar, state_path(unit))]  # fmt: skip
    return properties, env, argv


def unit_file(properties, env, argv):
    """The text of enable's unit file."""
    lines = ["# Written by `beanjar-web enable`; `beanjar-web disable` removes it.", "[Unit]",
             f"Description={DESCRIPTION}", "", "[Service]"]  # fmt: skip
    lines += [f"{k}={v}" for k, v in properties]
    lines += [f"Environment={systemd_quote(f'{k}={v}')}" for k, v in sorted(env.items())]
    lines.append("ExecStart=" + " ".join(systemd_quote(w, exec_line=True) for w in argv))
    lines += ["", "[Install]", "WantedBy=default.target", ""]
    return "\n".join(lines)


def wait_listening(unit, since, done):
    """Wait until the unit's server listens and print done with where it listens: 0. Else the
    unit is stopped (if still starting) and its journal shown: 1."""
    end = time.monotonic() + WAIT
    while time.monotonic() < end:
        state = read_state(unit)
        active, pid = props(unit)
        if state and active == "active":
            print(f"beanjar-web: {done} (systemd user unit {unit}, pid {pid})")
            for line in describe(state):
                print(f"beanjar-web: {line}")
            return 0
        if active not in UP:
            break
        time.sleep(0.2)
    else:
        systemctl("stop", f"{unit}.service")
    log = subprocess.run(
        ["journalctl", "--user", "-u", f"{unit}.service", "--since", f"@{int(since)}",
         "-o", "cat", "--no-pager", "-n", "20"],
        capture_output=True, text=True,
    ).stdout.strip()  # fmt: skip
    return fail(f"the unit {unit} did not start listening" + (f":\n{log}" if log else ""))


def start(a, beanjar_command, with_options=True):
    unit = unit_name()
    if not have_systemd():
        return fail("no systemd user manager (systemctl --user): run `beanjar-web serve` instead")
    active, pid = props(unit)
    if active in UP:
        return fail(
            f"already running (unit {unit}, pid {pid}): `beanjar-web status` prints its URL, "
            "`beanjar-web stop` stops it"
        )
    if persistent(unit):
        if with_options:
            return fail(
                f"unit {unit} is enabled with its options in {unit_path(unit)}: `beanjar-web "
                "start` without options starts it, `beanjar-web enable OPTIONS` changes them, "
                "`beanjar-web disable` removes it"
            )
        since = time.time()
        systemctl("reset-failed", f"{unit}.service")
        r = systemctl("start", f"{unit}.service")
        if r.returncode:
            return fail(f"systemctl --user start {unit}: {r.stderr.strip()}")
        return wait_listening(unit, since, "started")
    spec = service(a, beanjar_command, unit)
    if isinstance(spec, str):
        return fail(spec)
    properties, env, argv = spec
    systemctl("reset-failed", f"{unit}.service")
    linger()
    since = time.time()
    cmd = ["systemd-run", "--user", "--quiet", "--unit", unit, "--collect",
           "--description", DESCRIPTION]  # fmt: skip
    for k, v in properties:
        cmd += ["-p", f"{k}={v}"]
    for k, v in sorted(env.items()):
        cmd += ["-E", f"{k}={v}"]
    r = subprocess.run(cmd + argv, capture_output=True, text=True)
    if r.returncode:
        return fail(f"systemd-run failed: {r.stderr.strip()}")
    return wait_listening(unit, since, "started")


def enable(a, beanjar_command):
    """The unit file written (or rewritten with these options), enabled, lingering on, and the
    service (re)started from it; a transient unit of that name is stopped first."""
    unit = unit_name()
    if not have_systemd():
        return fail("no systemd user manager (systemctl --user)")
    spec = service(a, beanjar_command, unit)
    if isinstance(spec, str):
        return fail(spec)
    try:
        text = unit_file(*spec)
    except ValueError as e:
        return fail(f"cannot write a unit file: {e}")
    active, _pid = props(unit)
    if active in UP:
        r = systemctl("stop", f"{unit}.service")
        if r.returncode:
            return fail(f"systemctl --user stop {unit}: {r.stderr.strip()}")
        print(f"beanjar-web: stopped the running unit {unit}, to start it from the unit file")
    systemctl("reset-failed", f"{unit}.service")
    path = unit_path(unit)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)
    for words in (("daemon-reload",), ("enable", f"{unit}.service")):
        r = systemctl(*words)
        if r.returncode:
            return fail(f"systemctl --user {' '.join(words)}: {r.stderr.strip()}")
    linger()
    since = time.time()
    r = systemctl("start", f"{unit}.service")
    if r.returncode:
        return fail(f"systemctl --user start {unit}: {r.stderr.strip()}")
    return wait_listening(unit, since, f"enabled, starts at boot ({path}); started")


def disable():
    unit = unit_name()
    if not have_systemd():
        return fail("no systemd user manager (systemctl --user)")
    path = unit_path(unit)
    if not os.path.isfile(path):
        active, _pid = props(unit)
        more = ": `beanjar-web stop` stops the running one" if active in UP else ""
        print(f"beanjar-web: not enabled (no {path}){more}")
        return 0
    r = systemctl("disable", "--now", f"{unit}.service")
    if r.returncode:
        return fail(f"systemctl --user disable --now {unit}: {r.stderr.strip()}")
    os.remove(path)
    systemctl("daemon-reload")
    systemctl("reset-failed", f"{unit}.service")
    print(f"beanjar-web: disabled and stopped (unit {unit}); removed {path}")
    return 0


def kind(unit):
    """status's word for the unit: persistent (enable's file, and whether systemd starts it at
    boot) or transient (start's, gone at reboot)."""
    if persistent(unit):
        state = enabled(unit)
        return "persistent, " + ("starts at boot" if state == "enabled" else f"{state or '?'}")
    return "transient: gone at reboot"


def stop():
    unit = unit_name()
    if not have_systemd():
        return fail("no systemd user manager (systemctl --user)")
    active, _pid = props(unit)
    if active not in UP:
        print(f"beanjar-web: not running (unit {unit})")
        return 0
    r = systemctl("stop", f"{unit}.service")
    if r.returncode:
        return fail(f"systemctl --user stop {unit}: {r.stderr.strip()}")
    if persistent(unit):
        print(f"beanjar-web: stopped (unit {unit}) for now; still enabled: it starts at boot, "
              "`beanjar-web start` starts it again, `beanjar-web disable` removes it")  # fmt: skip
    else:
        print(f"beanjar-web: stopped (unit {unit})")
    return 0


def status():
    unit = unit_name()
    if not have_systemd():
        return fail("no systemd user manager (systemctl --user)")
    active, pid = props(unit)
    if active not in UP:
        more = f"; {kind(unit)}" if persistent(unit) else ""
        print(f"beanjar-web: not running (unit {unit}{more})")
        return 3
    state = read_state(unit)
    if not state:
        print(f"beanjar-web: {active}, not listening yet (unit {unit}, pid {pid}, {kind(unit)})")
        return 0
    print(f"beanjar-web: running (unit {unit}, pid {pid}, {kind(unit)})")
    for line in describe(state):
        print(f"beanjar-web: {line}")
    return 0
