# Inspect emulator traffic (`mode = "mitm"`)

`mode = "mitm"` (the launcher's `NET_MODE=mitm`), the default, runs **mitmweb in transparent mode** inside the emulator's network
namespace. Every guest connection to tcp/80 or tcp/443 goes through it, whatever the host (LG's
included): it is recorded, decrypted when the guest trusts our CA, and forwarded live to its real
destination. Nothing is answered locally and no host is special. Everything else the guest sends
(other ports, UDP, DNS) goes out directly, as with `mode = "direct"`. The settings are in
[docs/dev/architecture/network.md](../dev/architecture/network.md). Started with
`NETSTUB_WEB_HOST=0.0.0.0` (below), the inspector is at **http://HOST-ADDRESS:18083/** (with its
password, below).

Open the inspector before launching the screens you want to see. Useful filters: `~d lgtvsdp.com`
for SDP, `~u authentication` for authentication calls, `~d lge.com` for LG services. Select a
flow to see its request and response headers and body. The inspector keeps flows in memory for the
current VM run, and restarting the VM clears them; `beanjar mitm clear NAME` empties the list
without a restart. It keeps the last 1000 flows: beyond that the
oldest finished ones are dropped, from memory and from the view (`beanjar settings NAME set mitm_flows=N`
while the VM is off, `0` for all of them, `unset mitm_flows` for 1000 again; `MITM_FLOWS` for `vm-launcher`). `mitm_flow_bytes=200M`
also bounds the bytes of bodies they hold (off by default; `MITM_FLOW_BYTES`): beyond it the
oldest finished flows go too. Live updates use the same web port.

For example, from the checkout:

```sh
NETSTUB_WEB_HOST=0.0.0.0 DISPLAY_BACKEND=dbus LAYERS=normal,gl,rootssh,devmode host/scripts/vm-launcher start
```

The host bridge listens on `NETSTUB_WEB_HOST` (default the VM's own address, `127.0.1.<slot>`,
this machine only; `beanjar info` and `host/scripts/vm-launcher config ADDRESS` print it) and
`NETSTUB_WEB_PORT` (default `18083`). `NETSTUB_WEB_HOST=0.0.0.0` opens it to the LAN; from another
machine, an SSH tunnel to `127.0.1.<slot>:18083` does the same without that. A `link = "lan"` VM
serves it on its own LAN address as well (`http://LAN-ADDRESS:18083/`, the same password), beside
its TV-service ports; the host itself still uses `127.0.1.<slot>`, since it cannot reach its own
macvlan children. It shows raw device
and session headers, so redact exports before sharing them.

## The password

Every inspector needs a password, on loopback and on the LAN alike. Each VM start makes a random
one, unless `BEANJAR_MITM_PASSWORD` is set in the environment of the start (`beanjar start`, the
manager, `vm-launcher`), which is then used instead. It is kept in an owner-only file in the VM's
run directory, `<vm>/run/mitmweb.password` (the checkout VM's `build/mitmweb.password`), and
removed when the VM stops. It never appears on a command line, in a unit's environment or in a log.

- **Manager:** the Traffic Inspector pane shows the password with **Copy Password**. **Open in
  Browser** opens a link that logs in by itself; **Copy URL** copies the plain address. For a
  `link = "lan"` VM the address shown and copied is the LAN one, and "On this host" the local one,
  which Open in Browser uses (the browser runs on the host).
- **CLI:** `beanjar info NAME` gives `inspector.url` (the LAN URL for a `link = "lan"` VM, with
  `inspector.local_url` for the host itself) and `inspector.password_file`, the file's
  path rather than the password, because status output ends up in terminals, logs and issues:
  `cat` that file for the password.
- **Browser:** mitmweb shows a login page; enter the password. A link of the form
  `http://ADDRESS:18083/?token=PASSWORD` logs in directly and sets a cookie for that mitmweb run.
- **Scripts:** send `Authorization: Bearer PASSWORD`; without it mitmweb answers 403.

mitmweb holds only an argon2 hash of the password (`mitm_addon.py` reads the file). A password
is set once per start: restarting the VM makes a new one unless `BEANJAR_MITM_PASSWORD` fixes it.

## What the flows mean

- **HTTP flows with a response** were forwarded live. The response is the real server's, errors
  included: nothing is replaced with a local answer.
- **"Streamed body, not recorded"**: a large request or response (over `NETSTUB_STREAM_LARGE`,
  default 1 MiB), or an open-ended binary one, passed through without being kept.
- **TLS handshake errors** in the event log: the guest rejected our certificate (pinning, or a
  trust store without our CA), or the proxy could not reach the server or complete its TLS
  handshake (a 502 flow for a server-side failure). That connection fails; subsequent connections
  to the same named endpoint and port pass through undecrypted and appear as **TCP flows**.
  Without SNI, the bypass applies to the destination address and port. `build/netstub.log`
  records `tls-failed` and `tls-passthrough` events. Automatic bypass rules are bounded to 256
  endpoints per proxy run, with the oldest evicted; manual `ignore_hosts` rules are preserved.
  Bypassing interception cannot repair an unreachable upstream server.
- The proxy does not verify the servers' certificates (`ssl_insecure`), so LG's `*.lgtvsdp.com`
  (private root, CN-only) is decrypted like any other host.
- The guest trusts our CA (the library's `ca/`, the checkout VM's `local/ca`), which it appends at
  boot to the firmware's system bundle, sdx's `sdp-ca.pem` and the hashed `/etc/ssl/certs`.
  The firmware's hosts file, routes and `http://lgtvonline.lge.com/` connectivity
  check stay stock.
- The device identity (the VM's provisioned model key) is loaded
  whenever the VM has one (`<vm>/identity/sestore.toml`, including managed development VMs),
  whatever the network is.

## Troubleshooting

- `build/netstub-mitm.log`: transparent mitmweb startup and proxy errors.
- `build/netns-relay.log`: the host-side port relay (the inspector and the forwarded ports),
  including a port it could not bind.
- `build/netns-relay-inner.log`: the namespace-side relay (connections to mitmweb and the guest).
- `build/netns-helper.log`: DHCP helper output.
- `build/netns-uplink.log`: the slirp4netns uplink.
- `build/netstub.log`: TLS handshake failures (`tls-failed`).
- `build/qemu.out`: VM launcher output, including the inspector bind address and the password
  file's path.

The launcher checks the proxy and web listeners before starting QEMU and aborts
if mitmweb fails to start. Guest SSH/developer ports retain their loopback-only
bindings.
