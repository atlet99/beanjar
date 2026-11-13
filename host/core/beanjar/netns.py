# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""What runs QEMU for mode=mitm and link=off (qemu.py's NET_LINK and NET_MODE): the launcher calls mitm() or off() with
QEMU's argv in place of running QEMU itself.

mitm(): a rootless network namespace, no sudo and no persistent host state. It makes a directory for
the port-bridge unix sockets (NETNS_SOCK_DIR, which the VM's unit sets to its RuntimeDirectory, or a
new webos-netns-* in TMPDIR), bridges the mitmweb inspector and the loopback-forwarded ports
(NETNS_FWD: layermeta.forwards, e.g. SSAP 3000/3001, root SSH 6622, devmode 9922/9991), all on the VM's own address VM_ADDRESS (127.0.1.<slot>,
qemu.py; the forwards on LISTEN_ADDRESS when vm.toml's listen sets it), into the namespace over those sockets (beanjar-relay, host/helpers/network/relay.c, one
process on each side), runs inner() under `unshare -rn` (a user+net namespace) and gives that
namespace its uplink to the internet with slirp4netns. inner() sets up the TAP, the routing of
the guest's TCP and UDP into mitmweb and DHCP, then runs QEMU. When QEMU exits the namespace is gone; the
helpers are killed and the socket dir removed.

  python3 -m beanjar.netns inner QEMU-ARGV...     (what mitm() runs under unshare -rn)
  python3 -m beanjar.netns lan QEMU-ARGV...       (link=lan: what mitm() runs under unshare -rnm)

The namespace is a router with no NAT and no netfilter. slirp4netns runs on the guest's own
network, 10.0.2.0/24, as QEMU's slirp does for mode=direct, and the namespace sits between the two
by proxy ARP: the guest's 10.0.2.15 is routed to the TAP, and each side's ARP for the other side's
addresses is answered by the namespace, so slirp sees the guest's packets from 10.0.2.15 unchanged
(a source outside its network it drops). The guest's TCP and UDP, every port, are routed instead
into mitmweb's own TUN device (`--mode tun`, TUN_DEV below) by policy rules; mitmweb forwards each
connection to its real destination itself, and ICMP goes to slirp unchanged. Apart from that the
guest's network is mode=direct's: DHCP (10.0.2.15, router 10.0.2.2), real DNS (slirp4netns's
resolver, 10.0.2.3, through mitmweb). mitmweb is the user's own mitmproxy (mitmtool.py: NETNS_MITMWEB,
PATH or ~/.local/bin, 12.0.1 or newer); without it the guest goes out directly. Env:
NETNS_FWD ("hostport:guestport ..."), NETNS_TAP, NETSTUB_WEB_HOST/NETSTUB_WEB_PORT,
NETSTUB_LOG, NETSTUB_STREAM_LARGE, MITM_FLOWS and MITM_FLOW_BYTES (mitmweb's max_flows and max_flow_bytes, in bytes), NETNS_LOG_DIR (the helpers' logs, default build/),
BEANJAR_MITM_PASSWORD / NETSTUB_WEB_PASSWORD_FILE (the inspector's password, PASSWORD_FILE below),
NETNS_SOCK_DIR, NETSTUB_CA (the proxy's CA, mitmweb's confdir; qemu.py sets it), VM_ADDRESS (qemu.py sets it).
NETNS_MITMWEB (mitmweb's path, mitmtool.py).
The inspector's host side listens on NETSTUB_WEB_HOST (default VM_ADDRESS;
0.0.0.0 opens it to the LAN), behind a password wherever it listens (PASSWORD_FILE below).

link = "lan" (NET_LINK=lan, mode mitm): the same inner namespace and guest, one level further in.
mitm() runs lan() under `unshare -rnm` (the LAN namespace, a user, network and mount namespace of
its own) and has the CAP_NET_ADMIN helper (NET_LAN_HELPER, host/helpers/network/lan-helper.c) make
a macvlan, lan0, on NET_LAN_PARENT with the VM's MAC (NET_LAN_MAC) straight into it. lan()
configures lan0 from a DHCP lease (busybox udhcpc, with host/helpers/network/lan_dhcp.py as its
event script, released when the VM stops) or the static NET_ADDRESS ("A.B.C.D/NN,GATEWAY", the
gateway also the DNS server), points its own /etc/resolv.conf (a bind mount, in its mount
namespace only) at the lease's DNS, writes the lease to RUN_DIR/lan.json (what status, list and
the GUI show), then runs inner() under `unshare -rn` with slirp4netns as before, now inside the
LAN namespace: the guest's traffic, mitmweb's upstream included, leaves from the VM's own LAN
address and MAC. A relay in the LAN namespace serves the TV-service ports (NETNS_FWD), the
inspector (NETSTUB_WEB_PORT, behind the same password) and VNC (VNC_PORT, beanjar vnc's while it
runs, from its unix socket VNC_SOCK) on that address; the host-side relay keeps the first two on
VM_ADDRESS (the inspector on NETSTUB_WEB_HOST) too, since the host itself cannot reach its own
macvlan children, and beanjar-vnc listens there itself. lan0 lives only in the LAN namespace, so it is
deleted at a stop, and destroyed with the namespace however the VM stops (every helper there dies
with its parent, PR_SET_PDEATHSIG): no interface outlives the VM. After a kill the kernel may keep
that namespace, and so lan0 and the VM's MAC on the parent, until its last TCP sockets are torn
down, minutes at most; the next start waits LAN_MAC_WAIT for it.

off(): the TV with no cable plugged in. The guest keeps its NIC (virtio-net, the board MAC), as a TV
keeps its Ethernet port, but the link is down from its first look, so connman sees a wired
interface with no carrier. The netdev is a hub port alone on its hub, so there is nothing on the
other side even if the guest could raise the link: no egress, structurally. QEMU has no
command-line option for a NIC that starts with its link down, so QEMU starts paused (-S) with a
private QMP socket, the link is set down (QMP set_link on net0) and QEMU resumed.
"""

import ctypes
import ipaddress
import json
import os
import secrets
import select
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

from . import mitmtool, paths, vmsettings


class Error(Exception):
    pass


# The guest's network, mode=direct's (QEMU's slirp defaults). slirp4netns serves it (the router,
# its DNS forwarder, and .100, its own address for the namespace, on up0). DHCP_SERVER is the
# TAP's only address, the DHCP server's identifier: an address slirp does not have, so the guest's
# unicast renewals reach this namespace's DHCP server rather than being proxy-ARPed to slirp's.
CIDR = "10.0.2.0/24"
GUEST = "10.0.2.15"
ROUTER = "10.0.2.2"
DNS = "10.0.2.3"
DHCP_SERVER = "10.0.2.254"

# The inspector: mitmweb's own `--mode tun:TUN_DEV`. mitmweb makes the TUN device itself in this
# namespace (its user namespace's CAP_NET_ADMIN is enough) and reads each connection's destination
# from its packets, so no mitmproxy internals are patched for that. All of the guest's TCP and UDP,
# every port (only what arrives on the TAP: mitmweb's own upstream connections are not, and leave
# by the main table's default route), is routed into the device by policy rules (TUN_PROTOCOLS) to
# TUN_TABLE, whose one route is the device; ICMP is not matched and goes to slirp. The replies come
# out of the device and are forwarded to the TAP like any other packet. mitm_addon.py raises
# mitmproxy's UDP idle timeout (its TUN_UDP_TIMEOUT: an idle UDP flow closed in Python while the
# TUN side keeps the 5-tuple lost the guest's DNS). docs/research/network-proxy.md has the
# measurements. mitmweb is the user's own (mitmtool.py); what this start found (its path, version
# and, when it cannot serve, the problem) goes to RUN_DIR/MITM_INFO_FILE.
TUN_DEV = "mitm0"
TUN_TABLE = "101"
MITM_INFO_FILE = "mitmweb.json"


def mitm_info(run_dir):
    """What the VM's mitmweb is (RUN_DIR/MITM_INFO_FILE: version, path, problem), or None."""
    try:
        with open(os.path.join(run_dir, MITM_INFO_FILE)) as f:
            info = json.load(f)
        return info if isinstance(info, dict) else None
    except (OSError, ValueError, TypeError):
        return None


# The inspector's password (mitmweb's web_password). BEANJAR_MITM_PASSWORD supplies one; otherwise
# each start makes a random one. It travels only in an owner-only file, PASSWORD_FILE in RUN_DIR,
# whose path is NETSTUB_WEB_PASSWORD_FILE: never in argv (systemd-run --setenv, mitmweb's command
# line) or in a log. mitm_addon.py reads it into mitmweb; the manager (inspector.py) reads it to
# show it and to log in.
PASSWORD_ENV = "BEANJAR_MITM_PASSWORD"
PASSWORD_FILE = "mitmweb.password"

# beanjar vnc (core.Library.vnc): with link=lan it also listens on VNC_SOCK in the socket dir, which
# the LAN namespace's relay serves on the LAN address at VNC_PORT. A connection there while it is
# not running is closed.
VNC_PORT = 5900
VNC_SOCK = "vnc.sock"


LAN_IF = "lan0"  # the macvlan in the LAN namespace (lan-helper.c's IFNAME)
LAN_FILE = "lan.json"  # RUN_DIR/lan.json: the VM's LAN address while it runs (VM.lan_address)
LAN_LEASE_WAIT = 30  # s for a DHCP lease
LAN_MAC_WAIT = 60  # s for a previous run's lan0 to let go of the VM's MAC


def _die_with_parent():
    """preexec_fn: SIGKILL when the parent dies (prctl PR_SET_PDEATHSIG), so that nothing of a
    namespace outlives the process that made it (and holds its lan0)."""
    ctypes.CDLL(None, use_errno=True).prctl(1, signal.SIGKILL)


def lan_info(run_dir):
    """The LAN lease of the VM whose run directory is run_dir (RUN_DIR/lan.json), or None."""
    try:
        with open(os.path.join(run_dir, LAN_FILE)) as f:
            info = json.load(f)
        ipaddress.IPv4Address(info["address"])
        return info
    except (OSError, ValueError, KeyError, TypeError):
        return None


def write_password(path, password="", source=PASSWORD_ENV):
    """Write password (a new random one if empty; source: where a given one came from) to path,
    owner-only, replacing what was there."""
    password = password or secrets.token_urlsafe(18)
    if any(c in password for c in "\r\n"):
        raise Error(f"{source} must be one line")
    tmp = f"{path}.{os.getpid()}.tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(password + "\n")
    os.replace(tmp, path)
    return path


def _sbin_path(env):
    """ip lives in /sbin or /usr/sbin, which a non-login ssh shell (a Pi over `ssh host
    ...`) leaves out of PATH."""
    path = env.get("PATH", os.defpath)
    if "/usr/sbin" not in path.split(":"):
        env["PATH"] = path + ":/usr/sbin:/sbin"
    return env


class _Stop(Exception):
    def __init__(self, sig):
        self.sig = sig


def _raise_on(*sigs):
    """Raise _Stop on the first of sigs only: a second signal arriving while the finally block
    starts (before it ignores them) must not abort the cleanup."""
    raised = []

    def handler(sig, _frame):
        if not raised:
            raised.append(sig)
            raise _Stop(sig)

    for s in sigs:
        signal.signal(s, handler)


def _kill(procs):
    for p in procs:
        if p.poll() is None:
            try:
                p.terminate()
            except ProcessLookupError:
                pass
    for p in procs:
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()


def _relay(tree, pairs, env, log_path, who):
    """Start beanjar-relay (host/helpers/network/relay.c) for pairs (LISTEN=CONNECT), its stderr in
    log_path; return it once every listener is bound, or raise Error with its reason."""
    with open(log_path, "w") as log:
        p = subprocess.Popen(
            [tree.relay(), *pairs],
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=log,
            preexec_fn=_die_with_parent,
        )
    with p.stdout:
        ok = select.select([p.stdout], [], [], 10)[0] and p.stdout.readline() == b"ready\n"
    if not ok:
        _kill([p])
        try:
            with open(log_path) as f:
                why = f.read().strip().splitlines()[-1:]
        except OSError:
            why = []
        raise Error(f"{who}: port relay failed: {why[0] if why else 'no reply'} (see {log_path})")
    return p


def relay_pairs(env, rundir):
    """The host side's relay (LISTEN=CONNECT, beanjar-relay): the inspector on NETSTUB_WEB_HOST and
    NETSTUB_WEB_PORT, the layers' forwards (NETNS_FWD) on LISTEN_ADDRESS, else VM_ADDRESS (qemu.py)."""
    listen = env.get("LISTEN_ADDRESS") or env["VM_ADDRESS"]
    pairs = [f"tcp:{env['NETSTUB_WEB_HOST']}:{env['NETSTUB_WEB_PORT']}=unix:{rundir}/mitmweb.sock"]
    return pairs + [
        f"tcp:{listen}:{hp}=unix:{rundir}/fwd-{hp}.sock"
        for hp in (f.split(":", 1)[0] for f in env.get("NETNS_FWD", "").split())
    ]


def lan_relay_pairs(env, rundir):
    """link=lan's relay in the LAN namespace (LISTEN=CONNECT, beanjar-relay): the inspector on
    NETSTUB_WEB_PORT, VNC on VNC_PORT and the layers' forwards (NETNS_FWD), on every address there
    (lo and lan0)."""
    pairs = [
        f"tcp:0.0.0.0:{env['NETSTUB_WEB_PORT']}=unix:{rundir}/mitmweb.sock",
        f"tcp:0.0.0.0:{VNC_PORT}=unix:{rundir}/{VNC_SOCK}",
    ]
    return pairs + [
        f"tcp:0.0.0.0:{hp}=unix:{rundir}/fwd-{hp}.sock"
        for hp in (f.split(":", 1)[0] for f in env.get("NETNS_FWD", "").split())
    ]


def _status(rc):
    return 128 - rc if rc < 0 else rc


def _netns(pid):
    try:
        return os.readlink(f"/proc/{pid}/ns/net")
    except OSError:
        return None


def _wait_netns(child):
    """Wait until child (a Popen of unshare) has its own network namespace."""
    mine = _netns(os.getpid())
    for _ in range(100):
        if _netns(child.pid) != mine:
            break
        time.sleep(0.05)
    if _netns(child.pid) in (mine, None):
        raise Error("netns: namespace did not appear")


def _uplink(child, env, logdir):
    """slirp4netns for child's namespace (inner()'s): the guest's network's router (.2), DNS (.3)
    and NAT, on interface up0, from this namespace (the host's, or link=lan's LAN namespace)."""
    with open(os.path.join(logdir, "netns-uplink.log"), "w") as log:
        return subprocess.Popen(
            [
                "slirp4netns",
                "--configure",
                "--mtu=65520",
                "--disable-host-loopback",
                f"--cidr={CIDR}",
                str(child.pid),
                "up0",
            ],
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=log,
            preexec_fn=_die_with_parent,
        )


def mitm(argv, env, tree=paths.TREE):
    """Run QEMU (argv) in the mode=mitm namespace; QEMU's exit status."""
    env = _sbin_path(dict(env))
    lan = env.get("NET_LINK") == "lan"
    for tool in ("unshare", "ip", "slirp4netns") + (("busybox",) if lan else ()):
        if not shutil.which(tool, path=env["PATH"]):
            raise Error(
                f"netns: {tool} not found (needed for mode=mitm; mode=direct needs none of them)"
            )
    if lan:
        for k in ("NET_LAN_PARENT", "NET_LAN_MAC", "NET_LAN_HELPER"):
            if not env.get(k):
                raise Error(f"netns: {k} unset (qemu.py sets it for link=lan)")
    # Fail early and clearly if unprivileged user namespaces are unavailable, rather than needing root.
    if (
        subprocess.run(["unshare", "-rn", "true"], env=env, stderr=subprocess.DEVNULL).returncode
        != 0
    ):
        raise Error(
            "netns: rootless 'unshare -rn' unavailable on this host (need unprivileged userns); use mode=direct"
        )
    if env.get("NETNS_SOCK_DIR"):
        rundir = env["NETNS_SOCK_DIR"]
        os.makedirs(rundir, exist_ok=True)
    else:
        rundir = tempfile.mkdtemp(prefix="webos-netns-", dir=env.get("TMPDIR") or "/tmp")
    env["NETNS_RUNDIR"] = rundir
    # every host port on the VM's own address (qemu.py: VM_ADDRESS, 127.0.1.<slot>), the
    # inspector's too unless NETSTUB_WEB_HOST says otherwise
    address = env.get("VM_ADDRESS")
    if not address:
        raise Error("netns: VM_ADDRESS unset (the VM's host address, qemu.py)")
    web_port = env["NETSTUB_WEB_PORT"] = env.get("NETSTUB_WEB_PORT") or "18083"
    web_host = env["NETSTUB_WEB_HOST"] = env.get("NETSTUB_WEB_HOST") or address
    logdir = env.get("NETNS_LOG_DIR") or tree.build()
    # the inspector's password: the launcher's file (launch.py, from BEANJAR_MITM_PASSWORD), else
    # BEANJAR_MITM_PASSWORD, else a new random one; only the file's path goes on to the namespace
    given = env.pop(PASSWORD_ENV, "")
    password = env.get("NETSTUB_WEB_PASSWORD_FILE")
    if given or not password or not os.path.isfile(password):
        password = write_password(os.path.join(env.get("RUN_DIR") or rundir, PASSWORD_FILE), given)
    env["NETSTUB_WEB_PASSWORD_FILE"] = password
    helpers, ns = [], None
    _raise_on(signal.SIGTERM, signal.SIGINT)
    try:
        # The inspector lives inside the namespace; only its web port is exposed on the host. The
        # forwarded ports: LISTEN_ADDRESS (default VM_ADDRESS):<hostport> -> shared unix socket -> (inner) guest:<guestport>.
        # stdin stays ours, so an interactive serial console (on stdio) still reads the terminal.
        penv = dict(
            env,
            PYTHONPATH=os.pathsep.join(x for x in (tree.pkg_parent, env.get("PYTHONPATH")) if x),
        )
        helpers.append(
            _relay(
                tree,
                relay_pairs(env, rundir),
                penv,
                os.path.join(logdir, "netns-relay.log"),
                "netns",
            )
        )
        _announce_inspector(
            env,
            f"traffic inspector (mitmweb): http://{web_host}:{web_port} (password in {password})",
        )
        ns = subprocess.Popen(
            [
                "unshare",
                "-rn",
                *(["-m", "--propagation", "private"] if lan else []),
                sys.executable,
                "-m",
                "beanjar.netns",
                "lan" if lan else "inner",
                *argv,
            ],
            env=penv,
            preexec_fn=_die_with_parent,
        )
        # Wait until the child has its own network namespace, then attach the uplink to it:
        # slirp4netns is a user-mode NAT on the guest's network (interface up0, router .2, DNS
        # .3), with the host's loopback services unreachable (--disable-host-loopback). For
        # link=lan the uplink is the macvlan instead, and lan() attaches slirp4netns itself.
        _wait_netns(ns)
        if lan:
            made = subprocess.run(
                [
                    env["NET_LAN_HELPER"],
                    "macvlan",
                    env["NET_LAN_PARENT"],
                    env["NET_LAN_MAC"],
                    str(ns.pid),
                ],
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            if made.returncode != 0:
                raise Error(f"netns: no macvlan on {env['NET_LAN_PARENT']}: {made.stdout.strip()}")
            return _status(ns.wait())
        helpers.append(_uplink(ns, env, logdir))
        return _status(ns.wait())
    except _Stop as e:
        return 128 + e.sig
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)  # cleanup runs to the end
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if ns is not None and ns.poll() is None:
            ns.terminate()  # inner() stops QEMU and its helpers
            try:
                ns.wait(timeout=25)
            except subprocess.TimeoutExpired:
                ns.kill()
                ns.wait()
        _kill(helpers)
        shutil.rmtree(rundir, ignore_errors=True)
        try:
            os.unlink(password)
        except OSError:
            pass


def _ip(env, *args):
    subprocess.run(["ip", *args], env=env, check=True)


def _sysctl(key, value):
    with open("/proc/sys/net/ipv4/" + key, "w") as f:
        f.write(value + "\n")


def _can_connect(port):
    try:
        socket.create_connection(("127.0.0.1", int(port)), 0.2).close()
        return True
    except OSError:
        return False


class Proxy:
    """mitmweb as _start_proxy started it: process, and ready once it listens (its addon's ready
    file and its web port), which the launcher does not wait for (_wait_guest polls it)."""

    def __init__(self, process, ready_file, web_port, log, info=None):
        self.process, self.ready_file, self.web_port, self.log = process, ready_file, web_port, log
        self.info = info or {}  # what RUN_DIR/MITM_INFO_FILE says of it
        self.started = time.monotonic()
        self.ready = False

    def _device(self):
        """Whether mitmweb's TUN device exists in this namespace yet."""
        try:
            socket.if_nametoindex(TUN_DEV)
            return True
        except OSError:
            return False

    def poll(self):
        """True once it is ready, False if it exited or has not been ready for PROXY_WAIT s, None
        while it is still starting."""
        if self.ready:
            return True
        if self.process.poll() is not None:
            return False
        if os.path.exists(self.ready_file) and _can_connect(self.web_port) and self._device():
            self.ready = True
            return True
        if time.monotonic() - self.started >= PROXY_WAIT:
            return False
        return None


# How long mitmweb may take to listen (s): on an RK3399 board with mitmproxy on NFS its first start
# took 10 s. The guest's TCP and UDP wait (held, _hold) until then.
PROXY_WAIT = 60
# The policy rules' priorities: the interception (_intercept) before the hold (_hold).
INTERCEPT_PREF = "100"
HOLD_PREF = "101"


def mitmweb_argv(mitmweb, tree, env, confdir, web_port):
    """mitmweb's command line: its TUN device TUN_DEV, with mitm_addon.py."""
    return [
        mitmweb,
        "--mode",
        f"tun:{TUN_DEV}",
        "--web-host",
        "127.0.0.1",
        "--web-port",
        web_port,
        "--no-web-open-browser",
        "--set",
        f"confdir={confdir}",
        "--set",
        "ssl_insecure=true",
        "--set",
        # every TCP port is carried, server-first protocols too (SSH, SMTP...), which lazy would
        # hold until the client spoke: eager dials upstream as the guest connects
        "connection_strategy=eager",
        "--set",
        "show_ignored_hosts=true",
        "--set",
        "termlog_verbosity=warn",
        "--set",
        "flow_detail=0",
        "--set",
        f"max_flows={env.get('MITM_FLOWS') or vmsettings.MITM_FLOWS}",  # mitm_addon.py's FlowLimit
        "--set",
        f"max_flow_bytes={env.get('MITM_FLOW_BYTES') or 0}",
        "-s",
        tree.src("host", "helpers", "network", "mitm_addon.py"),
    ]


def _write_info(env, rundir, info):
    """RUN_DIR/MITM_INFO_FILE: what this start's mitmweb is (mitm_info reads it)."""
    base = env.get("RUN_DIR") or rundir
    if not base:
        return
    path = os.path.join(base, MITM_INFO_FILE)
    try:
        tmp = f"{path}.{os.getpid()}.tmp"
        with open(tmp, "w") as f:
            json.dump(info, f)
        os.replace(tmp, path)
    except OSError as e:
        print(f"netns inner: could not write {path}: {e}", file=sys.stderr)


def _announce_inspector(env, line):
    """Print the inspector's address line, unless its mitmweb cannot be used: the namespace then
    says the inspector is off and why (_start_proxy), and an address would only mislead."""
    if mitmtool.check(env).usable:
        print(line, file=sys.stderr, flush=True)


def _start_proxy(env, tree, rundir, logdir, web_port):
    """Start the optional inspector, without waiting for it: a Proxy, or None if it cannot start
    (no mitmweb, or one older than mitmtool.MIN_VERSION: the reason is printed and recorded in
    RUN_DIR/MITM_INFO_FILE). Proxy failures leave the routed network usable."""
    tool = mitmtool.check(env)
    info = {"version": tool.version, "path": tool.path, "problem": tool.problem}
    _write_info(env, rundir, info)
    if not tool.usable:
        print(
            f"netns inner: traffic inspector off, forwarding directly: {tool.problem}",
            file=sys.stderr,
            flush=True,
        )
        return None
    print(f"netns inner: mitmweb {tool.version} ({tool.path})", file=sys.stderr, flush=True)
    proxy = None
    try:
        # mitmweb on its TUN device, with the proxy's CA (NETSTUB_CA) as its confdir. Upstream
        # certificates are not verified (ssl_insecure): the proxy is a debug tool, and LG's servers
        # (*.lgtvsdp.com) come from a private root no proxy-side store has. A TLS handshake failure
        # may fail one request; subsequent connections pass through undecrypted. The failed
        # request is not replayed. Ordinary connection failures remain network failures.
        ca = subprocess.run(
            [tree.src("host", "helpers", "network", "tlsca.py")],
            env=env,
            stdout=subprocess.PIPE,
            text=True,
            check=True,
        ).stdout.strip()
        env["NETSTUB_LOG"] = env.get("NETSTUB_LOG") or tree.build("netstub.log")
        mitm_log = os.path.join(logdir, "netstub-mitm.log")
        env["NETNS_MITM_READY"] = ready_file = os.path.join(rundir, "mitmweb.ready")
        if os.path.exists(ready_file):
            os.unlink(ready_file)
        for f in (mitm_log, env["NETSTUB_LOG"]):
            open(f, "a").close()
            os.chmod(f, 0o600)
        with open(mitm_log, "w") as log:
            proxy = subprocess.Popen(
                mitmweb_argv(tool.path, tree, env, os.path.dirname(ca), web_port),
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=log,
                stderr=log,
                preexec_fn=_die_with_parent,
            )
        return Proxy(proxy, ready_file, web_port, mitm_log, info)
    except (OSError, subprocess.CalledProcessError) as exc:
        if proxy is not None:
            _kill([proxy])
        print(f"netns inner: mitmweb unavailable: {exc}; forwarding directly", file=sys.stderr)
        _write_info(env, rundir, dict(info, problem=f"mitmweb did not start: {exc}"))
        return None


# All of the guest's TCP and UDP, every port (DNS, NTP and the rest included); other protocols
# (ICMP: ping, traceroute's replies) are not matched and are forwarded to slirp, since mitmweb's
# TUN stack handles only TCP and UDP.
TUN_PROTOCOLS = ("tcp", "udp")


def intercept_rules(tap, action):
    """The ip commands of _intercept: one policy rule per protocol (TUN_PROTOCOLS), for packets
    arriving on tap only, to TUN_TABLE. Packets to the namespace's own addresses (DHCP, the
    relays' replies) meet the local table first (pref 0)."""
    return [
        ["rule", action, "pref", INTERCEPT_PREF, "iif", tap, "ipproto", p, "lookup", TUN_TABLE]
        for p in TUN_PROTOCOLS
    ]


def hold_rules(tap, action):
    """The ip commands of _hold: the same selectors, blackholed."""
    return [
        ["rule", action, "pref", HOLD_PREF, "iif", tap, "ipproto", p, "blackhole"]
        for p in TUN_PROTOCOLS
    ]


def tun_route(action):
    """TUN_TABLE: every destination by mitmweb's device. The kernel drops it with the device when
    mitmweb exits; the rules then find no route there and fall through to the main table, the
    guest going straight out."""
    return ["route", action, "default", "dev", TUN_DEV, "table", TUN_TABLE]


def _intercept(env, tap, action):
    """Only the ingress policy rules divert traffic; the private table is inert without them."""
    if action == "add":
        _ip(env, *tun_route("replace"))
    for rule in intercept_rules(tap, action):
        _ip(env, *rule)


def _hold(env, tap, action):
    """While mitmweb starts, the guest's TCP and UDP are dropped (a blackhole rule), not refused or
    let through: a connection the guest opens then is not reset or failed, its SYN is sent again
    (TCP's retransmission: after 1, 3, 7, 15 s ...) and reaches mitmweb once it listens; a lost
    DNS query is asked again. Letting it through directly instead would have it reset when the
    interception starts, as its later packets would then be delivered to a mitmweb that has no
    such connection."""
    for rule in hold_rules(tap, action):
        _ip(env, *rule)


def _wait_guest(qemu, proxy, env, tap):
    """Keep the guest alive: start intercepting once its proxy (a Proxy) is ready, and withdraw
    the hold, or only the hold if it never gets ready; withdraw interception as soon as it exits."""
    pending = proxy
    while True:
        if pending is not None:
            ready = pending.poll()
            if ready:
                _intercept(env, tap, "add")  # before the hold goes: no SYN slips past
                _hold(env, tap, "del")
                print(
                    f"netns inner: mitmweb ready {time.monotonic() - pending.started:.1f} s after "
                    "its start; intercepting",
                    file=sys.stderr,
                    flush=True,
                )
                pending = None
            elif ready is False:
                _kill([pending.process])
                _hold(env, tap, "del")
                _write_info(
                    env,
                    env.get("NETNS_RUNDIR"),
                    dict(pending.info, problem=f"mitmweb did not start (see {pending.log})"),
                )
                print(
                    f"netns inner: mitmweb unavailable (see {pending.log}); forwarding directly",
                    file=sys.stderr,
                    flush=True,
                )
                pending = proxy = None
        elif proxy is not None and proxy.process.poll() is not None:
            _intercept(env, tap, "del")
            print("netns inner: mitmweb exited; forwarding directly", file=sys.stderr)
            _write_info(
                env,
                env.get("NETNS_RUNDIR"),
                dict(proxy.info, problem=f"mitmweb exited (see {proxy.log})"),
            )
            proxy = None
        try:
            return _status(qemu.wait(timeout=0.2))
        except subprocess.TimeoutExpired:
            pass


def _discovery(env, mode, logdir):
    """Discovery has its own system Python and lifetime, independent of mitmweb."""
    log_path = os.path.join(logdir, "netns-discovery-" + mode + ".log")
    with open(log_path, "w") as log:
        helper = subprocess.Popen(
            ["/usr/bin/python3", "-m", "beanjar.discovery", mode],
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=log,
            preexec_fn=_die_with_parent,
        )
    with helper.stdout:
        ready = (
            select.select([helper.stdout], [], [], 10)[0] and helper.stdout.readline() == b"ready\n"
        )
    if not ready or helper.poll() is not None:
        _kill([helper])
        raise Error(f"netns {mode}: discovery helper failed (see {log_path})")
    return helper


def inner(argv, tree=paths.TREE):
    """Inside the namespace (mitm() runs this under unshare -rn and attaches the uplink, up0): the
    TAP, proxy ARP between it and the uplink, the routing of TCP and UDP into mitmweb, DHCP, the
    port bridges into the guest, then QEMU in the foreground. The helpers are killed when QEMU
    exits."""
    env = _sbin_path(dict(os.environ))
    rundir = env.get("NETNS_RUNDIR")
    if not rundir:
        raise Error("netns inner: NETNS_RUNDIR not set")
    tap = env.get("NETNS_TAP") or "tap0"
    web_port = env.get("NETSTUB_WEB_PORT") or "18083"
    logdir = env.get("NETNS_LOG_DIR") or tree.build()
    helpers, qemu = [], None
    _raise_on(signal.SIGTERM, signal.SIGINT)
    try:
        # the TAP: QEMU's NIC. Its one address is the DHCP server's; the guest is routed to it.
        _ip(env, "link", "set", "lo", "up")
        _ip(env, "tuntap", "add", tap, "mode", "tap")
        _ip(env, "addr", "add", f"{DHCP_SERVER}/32", "dev", tap)
        _ip(env, "link", "set", tap, "up")
        _ip(env, "route", "add", f"{GUEST}/32", "dev", tap)

        def default_route():
            return subprocess.run(
                ["ip", "route", "show", "default"], env=env, stdout=subprocess.PIPE, text=True
            ).stdout.strip()

        # the uplink (mitm() attaches it once this namespace exists)
        for _ in range(100):
            if default_route():
                break
            time.sleep(0.1)
        if not default_route():
            raise Error(
                f"netns inner: slirp4netns uplink did not come up (see {logdir}/netns-uplink.log)"
            )

        # routing: the guest's packets are forwarded to slirp unchanged, and slirp's back to the
        # guest, each side's ARP for the other's addresses answered here (proxy ARP). Its TCP and
        # UDP go to mitmweb's TUN device instead once it is ready (_intercept, _wait_guest).
        _sysctl("ip_forward", "1")
        for dev in (tap, "up0"):
            _sysctl(f"conf/{dev}/proxy_arp", "1")
        # DHCP (fixed lease; router and DNS slirp's, the server the TAP's own address)
        with open(os.path.join(logdir, "netns-helper.log"), "a") as log:
            helpers.append(
                subprocess.Popen(
                    [
                        sys.executable,
                        tree.src("host", "helpers", "network", "netns_dhcp.py"),
                        "--iface",
                        tap,
                        "--server",
                        DHCP_SERVER,
                        "--lease",
                        GUEST,
                        "--router",
                        ROUTER,
                        "--dns",
                        DNS,
                    ],
                    env=env,
                    stdin=subprocess.DEVNULL,
                    stdout=log,
                    stderr=log,
                    preexec_fn=_die_with_parent,
                )
            )

        # Runtime files stay private even when the optional inspector is absent.
        os.umask(0o077)
        # mitmweb starts beside QEMU: the guest's TCP and UDP are held until it listens (_hold,
        # _wait_guest), long before the guest's network is up on any board measured
        proxy = _start_proxy(env, tree, rundir, logdir, web_port)
        if proxy is not None:
            helpers.append(proxy.process)
            _hold(env, tap, "add")
        # the inspector's socket for the host-side relay, and the loopback-forwarded ports into the
        # guest. The unix sockets live on the shared filesystem, so the host-side relay (mitm(),
        # outside the namespace) reaches them; this side connects on to the guest. Replaces slirp
        # hostfwd, which a TAP netdev does not provide.
        pairs = [f"unix:{rundir}/mitmweb.sock=tcp:127.0.0.1:{web_port}"]
        pairs += [
            f"unix:{rundir}/fwd-{hp}.sock=tcp:{GUEST}:{gp}"
            for hp, gp in (f.split(":", 1) for f in env.get("NETNS_FWD", "").split())
        ]
        helpers.append(
            _relay(tree, pairs, env, os.path.join(logdir, "netns-relay-inner.log"), "netns inner")
        )
        if env.get("NETNS_LAN_ADDRESS"):
            helpers.append(_discovery(env, "inner", logdir))
        # QEMU in the foreground
        qemu = subprocess.Popen(argv, env=env, preexec_fn=_die_with_parent)
        return _wait_guest(qemu, proxy, env, tap)
    except _Stop as e:
        return 128 + e.sig
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)  # cleanup runs to the end
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if qemu is not None and qemu.poll() is None:
            qemu.terminate()
            try:
                qemu.wait(timeout=20)
            except subprocess.TimeoutExpired:
                qemu.kill()
        _kill(helpers)


def _lan_static(env, address):
    """NET_ADDRESS's static "A.B.C.D/NN,GATEWAY" on lan0: the lease lan() would have had, the
    gateway as router and DNS server."""
    iface, gateway = (x.strip() for x in address.split(","))
    iface = ipaddress.IPv4Interface(iface)
    _ip(env, "addr", "add", str(iface), "dev", LAN_IF)
    _ip(env, "route", "add", "default", "via", gateway, "dev", LAN_IF)
    return {
        "address": str(iface.ip),
        "prefix": iface.network.prefixlen,
        "router": gateway,
        "dns": [gateway],
        "lease": 0,
    }


def dhcp_argv(env, script):
    """The DHCP client of link=lan in the LAN namespace: it sends NET_LAN_HOSTNAME, when set, as
    its hostname (option 12), which the LAN's router may register as a name."""
    argv = ["busybox", "udhcpc", "-f", "-R", "-i", LAN_IF, "-t", "5", "-T", "2", "-A", "5"]
    if env.get("NET_LAN_HOSTNAME"):
        argv += ["-x", f"hostname:{env['NET_LAN_HOSTNAME']}"]
    return argv + ["-s", script]


def lan(argv, tree=paths.TREE):
    """The LAN namespace (link=lan; mitm() runs this under `unshare -rnm` and has lan0 made in it):
    lan0's address (a DHCP lease or NET_ADDRESS), DNS, RUN_DIR/lan.json, inner() with its uplink
    and a relay of the TV-service ports and the inspector on the LAN address; all of it undone
    when QEMU exits."""
    env = _sbin_path(dict(os.environ))
    rundir = env.get("NETNS_RUNDIR")
    if not rundir:
        raise Error("netns lan: NETNS_RUNDIR not set")
    logdir = env.get("NETNS_LOG_DIR") or tree.build()
    info = os.path.join(env.get("RUN_DIR") or rundir, LAN_FILE)
    address = env.get("NET_ADDRESS") or "dhcp"
    env["NETSTUB_WEB_PORT"] = env.get("NETSTUB_WEB_PORT") or "18083"
    helpers, child = [], None
    _raise_on(signal.SIGTERM, signal.SIGINT)
    try:

        def has_lan():
            return (
                subprocess.run(
                    ["ip", "link", "show", LAN_IF],
                    env=env,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                ).returncode
                == 0
            )

        for _ in range(100):  # mitm() has the helper make it once this namespace exists
            if has_lan():
                break
            time.sleep(0.1)
        if not has_lan():
            raise Error(f"netns lan: no {LAN_IF} from the helper")
        _ip(env, "link", "set", "lo", "up")
        # A macvlan comes up only while no other on the parent has its MAC: a previous run's lan0
        # outlives its namespace's processes until the kernel lets that namespace go (its TCP
        # sockets' teardown can take minutes after a kill), so wait for it, up to LAN_MAC_WAIT.
        end = time.monotonic() + LAN_MAC_WAIT
        while True:
            up = subprocess.run(
                ["ip", "link", "set", LAN_IF, "up"],
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            if up.returncode == 0:
                break
            if "in use" not in up.stdout or time.monotonic() > end:
                raise Error(
                    f"netns lan: {LAN_IF} on {env.get('NET_LAN_PARENT')} did not come up: {up.stdout.strip()}"
                    + (
                        " (the VM's MAC is still another interface's there: a previous run's, which the"
                        " kernel has not let go yet; try again later)"
                        if "in use" in up.stdout
                        else ""
                    )
                )
            time.sleep(1)
        if address == "dhcp":
            lease_file = os.path.join(rundir, "lan-lease.json")
            with open(os.path.join(logdir, "netns-dhcp.log"), "w") as log:
                dhcp = subprocess.Popen(
                    dhcp_argv(env, tree.src("host", "helpers", "network", "lan_dhcp.py")),
                    env=dict(env, LAN_LEASE_FILE=lease_file),
                    stdin=subprocess.DEVNULL,
                    stdout=log,
                    stderr=log,
                    preexec_fn=_die_with_parent,
                )
            helpers.append(dhcp)
            end = time.monotonic() + LAN_LEASE_WAIT
            while not os.path.exists(lease_file):
                if dhcp.poll() is not None or time.monotonic() > end:
                    raise Error(
                        f"netns lan: no DHCP lease on {env.get('NET_LAN_PARENT')} "
                        f"(see {logdir}/netns-dhcp.log)"
                    )
                time.sleep(0.2)
            with open(lease_file) as f:
                lease = json.load(f)
        else:
            lease = _lan_static(env, address)
        # the lease's DNS for this namespace's own lookups (slirp4netns's forwarder reads it)
        if lease.get("dns"):
            resolv = os.path.join(rundir, "resolv.conf")
            with open(resolv, "w") as f:
                f.write("".join(f"nameserver {d}\n" for d in lease["dns"]))
            if (
                subprocess.run(["mount", "--bind", resolv, "/etc/resolv.conf"], env=env).returncode
                != 0
            ):
                print(
                    "netns lan: could not use the lease's DNS; the host's /etc/resolv.conf stays",
                    file=sys.stderr,
                    flush=True,
                )
        lease.update(
            parent=env.get("NET_LAN_PARENT"),
            mac=env.get("NET_LAN_MAC"),
            source="dhcp" if address == "dhcp" else "static",
        )
        tmp = f"{info}.{os.getpid()}.tmp"
        with open(tmp, "w") as f:
            json.dump(lease, f)
        os.replace(tmp, info)
        print(
            f"network: LAN address {lease['address']}/{lease['prefix']} on {lease['parent']} "
            f"({lease['source']}), MAC {lease['mac']}",
            file=sys.stderr,
            flush=True,
        )
        env["NETNS_LAN_ADDRESS"] = lease["address"]
        child = subprocess.Popen(
            ["unshare", "-rn", sys.executable, "-m", "beanjar.netns", "inner", *argv],
            env=env,
            preexec_fn=_die_with_parent,
        )
        _wait_netns(child)
        helpers.append(_uplink(child, env, logdir))
        # the TV-service ports, the inspector (its password as on the host side) and VNC on the
        # LAN address (every address of this namespace: lo and lan0)
        helpers.append(
            _relay(
                tree,
                lan_relay_pairs(env, rundir),
                env,
                os.path.join(logdir, "netns-relay-lan.log"),
                "netns lan",
            )
        )
        _announce_inspector(
            env,
            f"traffic inspector (mitmweb) on the LAN: http://{lease['address']}:{env['NETSTUB_WEB_PORT']}",
        )
        print(
            f"VNC (beanjar vnc, while it runs) on the LAN: vnc://{lease['address']}:{VNC_PORT}",
            file=sys.stderr,
            flush=True,
        )
        helpers.append(_discovery(env, "outer", logdir))
        return _status(child.wait())
    except _Stop as e:
        return 128 + e.sig
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)  # cleanup runs to the end
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if child is not None and child.poll() is None:
            child.terminate()  # inner() stops QEMU and its helpers
            try:
                child.wait(timeout=25)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
        _kill(helpers)  # udhcpc releases the lease (-R) as it exits
        # lan0 now, rather than with the namespace, which sockets of the guest's can hold for minutes
        # (the next start's lan0 needs the MAC)
        subprocess.run(
            ["ip", "link", "del", LAN_IF],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            os.unlink(info)
        except OSError:
            pass


def _qmp(path, qemu, before_cont=None):
    """set_link net0 down and cont over QEMU's QMP socket at path; QEMU (a Popen) paused until then."""
    for _ in range(300):
        s = socket.socket(socket.AF_UNIX)
        try:
            s.connect(path)
            break
        except OSError:
            s.close()
            if qemu.poll() is not None:
                raise Error(f"net-off: QEMU exited ({_status(qemu.returncode)})") from None
            time.sleep(0.1)
    else:
        raise Error("net-off: no QMP socket")
    with s, s.makefile("rw") as f:
        f.readline()  # greeting

        def cmd(command, **args):
            f.write(json.dumps({"execute": command, "arguments": args}) + "\n")
            f.flush()
            while True:
                reply = json.loads(f.readline())
                if "return" in reply:
                    return
                if "error" in reply:
                    raise Error(f"net-off: {command}: {reply['error'].get('desc')}")

        cmd("qmp_capabilities")
        cmd("set_link", name="net0", up=False)
        if before_cont is None:
            cmd("cont")
        else:
            before_cont()


def off(argv, env, before_cont=None):
    """Run QEMU (argv) for link=off; its exit status."""
    d = tempfile.mkdtemp(prefix="webos-netoff-", dir=env.get("TMPDIR") or "/tmp")
    qemu = None
    _raise_on(signal.SIGTERM, signal.SIGINT)
    try:
        qemu = subprocess.Popen(
            [*argv, "-S", "-qmp", f"unix:{d}/qmp.sock,server=on,wait=off"], env=env
        )
        try:
            _qmp(os.path.join(d, "qmp.sock"), qemu, before_cont)
        except (Error, OSError, ValueError) as e:
            print(e, file=sys.stderr)
            raise Error("net-off: could not set the link down; QEMU stopped") from None
        print("network: off (NIC present, link down)", file=sys.stderr, flush=True)
        rc = qemu.wait()
        qemu = None
        return _status(rc)
    except _Stop as e:
        return 128 + e.sig
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)  # cleanup runs to the end
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if qemu is not None and qemu.poll() is None:
            qemu.terminate()
            try:
                qemu.wait(timeout=20)
            except subprocess.TimeoutExpired:
                qemu.kill()
        shutil.rmtree(d, ignore_errors=True)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) < 2 or argv[0] not in ("inner", "lan"):
        print("usage: python3 -m beanjar.netns inner|lan QEMU-ARGV...", file=sys.stderr)
        return 2
    try:
        return (inner if argv[0] == "inner" else lan)(argv[1:])
    except (Error, OSError, subprocess.CalledProcessError) as e:
        print(e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
