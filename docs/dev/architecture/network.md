# Guest networking

A VM's network is two settings in its `vm.toml`:

```toml
[network]
link = "nat"        # nat | lan | off
mode = "mitm"       # mitm | direct   (ignored when link = "off")
listen = "0.0.0.0"  # link = "nat" only: where the TV-service ports listen (default: the slot address)
address = "dhcp"    # link = "lan" only: "dhcp" or "A.B.C.D/NN,gateway"
slot = 1            # assigned, not a setting
```

`link` says where the guest's NIC is attached, `mode` whether its traffic is intercepted. They are
set with `beanjar settings NAME set link=lan mode=direct` (and the GUI's Network page), and reach the launcher
as `NET_LINK` and `NET_MODE` (`NET_ADDRESS` for `address`). The defaults are `link = "nat"` and
`mode = "mitm"`. The topology has no special routing case for SDP or LG. Earlier canned-response,
selective relay and zero-egress-default designs, and the single `NET=off | direct | mitm` setting
these two replaced, are historical.

| `link` | `mode` | Guest connection | Host implementation |
|---|---|---|---|
| `nat` | `mitm` | NAT to the internet, with TCP ports 80 and 443 passing through mitmweb | Rootless network namespace, TAP, routing and slirp4netns uplink |
| `nat` | `direct` | NAT without the proxy | QEMU user networking |
| `lan` | `direct` | *Planned.* On the host's wired LAN as a TV of its own: the guest takes its own DHCP lease, SSDP works, nothing is intercepted | QEMU attaches the guest to `lan_parent` with macvtap, with the VM's MAC |
| `lan` | `mitm` | On the LAN at its own address and MAC, with SSDP/mDNS discovery and best-effort web capture | A LAN namespace gets a macvlan on `lan_parent` with the VM's MAC and its DHCP lease or static `address`; the private guest sits behind it, with service and discovery relays (below) |
| `off` | ignored | NIC present with link down and no uplink | Isolated QEMU hub port; QMP sets link down before execution resumes |

Rules the settings enforce:

- `listen` belongs to `link = "nat"` and `address` to `link = "lan"`; either one with another link is
  a validation error. Changing the link clears it in the same command (`set tv1 link=lan listen=`).
- `lan` needs the library's `lan_parent` (`beanjar library config set lan_parent=eth0`), the host's wired
  interface, named explicitly and never guessed: a VPN interface such as ZeroTier may sit in the
  same subnet as the LAN. It must exist, and a wireless one is refused, at `config` and at a `lan` start
  (`/sys/class/net/IF/wireless`): macvlan and macvtap generally fail on Wi-Fi.
- `lan` VMs never collide with each other's ports: each has its own LAN address. With macvlan the
  host itself cannot reach its own children unless it adds a host-side macvlan of its own.
- Privileges: a small helper with only `CAP_NET_ADMIN` creates the macvlan or macvtap on the allowed
  `lan_parent` only, with the VM's MAC, and hands it to that VM. The rest of Beanjar stays rootless.

`lan` with `direct` is not implemented yet: it validates and is stored, but a start refuses
it, "link=lan with mode=direct is not implemented yet". The guest's kernel command line keeps
`webos.net=mitm|direct|off`, informational, for the `nat` and `off` combinations.

## The proxy's start

mitmweb starts beside QEMU, not before it: the launcher does not wait for it to listen. Until it
does, the guest's tcp/80 and tcp/443 are held by policy rules of the inner namespace that drop them
(`blackhole`, `netns._hold`), and once mitmweb listens the interception rules are added and then
the hold removed (`netns._wait_guest`). A connection the guest opens meanwhile is not refused and
not reset: its SYN is dropped, TCP sends it again (after 1 s, 3 s, 7 s, ...), and a retransmission
after mitmweb is up is accepted by it. Letting such a connection go directly instead would get it
reset when interception starts, since its later packets would then be delivered to a mitmweb that
has no such connection. A mitmweb that exits, or has not listened within 60 s (`PROXY_WAIT`), has
the hold removed and the guest's web traffic goes directly, as with no inspector.

`tests/integration/network/test_netns_failopen.py` shows it (mode `late`): a stand-in proxy listens only 1.5 s
after the fake guest has its lease and opens its HTTP connection, which completes about 2 s after
it was opened, answered by the proxy. On an RK3399 board mitmweb took about 10 s to listen; the
guest's network is up some 30 s after QEMU starts, so in practice nothing is held, and the start no
longer waits those 10 s.

## `lan` with `mitm`

`netns.py`'s `lan()` puts the whole `nat`+`mitm` arrangement one namespace further in:

```
host ── VM_ADDRESS relay (127.0.1.N: service ports, inspector)
 └─ LAN namespace (unshare -rnm)   lan0: macvlan on lan_parent, the VM's MAC, the lease
     ├─ busybox udhcpc (lan_dhcp.py), released at stop; or the static address
     ├─ relay: the service ports, the inspector and VNC (5900) on 0.0.0.0, the LAN address
     ├─ slirp4netns ── the inner namespace's up0 (10.0.2.0/24)
     └─ inner namespace (unshare -rn): tap0, proxy ARP, mitmweb, the guest's DHCP, QEMU
```

- **The helper.** `beanjar-lan-helper macvlan PARENT MAC PID` (`host/helpers/network/lan-helper.c`),
  installed with the file capability `cap_net_admin=ep` by the package's postinst, is the only
  privileged step. It refuses anything but a physical, wired Ethernet `PARENT` (not wireless, a
  bridge, bond or virtual interface), a unicast MAC and a PID of the calling user in another network
  namespace, then runs `ip link add link PARENT name lan0 address MAC netns PID type macvlan mode
  bridge` with that one capability and an empty environment. lan0 is made inside the LAN namespace
  in one step and never exists on the host side. In a checkout the helper is
  `build/network/beanjar-lan-helper` and needs the same `setcap` to work. Any local user who can run
  the installed helper can put a macvlan with a MAC of their choosing on a wired interface of the
  host into a namespace of their own; that is the boundary of this design.
- **DHCP.** Debian's `busybox udhcpc` in the foreground on lan0, with
  `host/helpers/network/lan_dhcp.py` as its event script, which configures lan0 and writes the
  lease. It runs rootless, as root of the LAN namespace's user namespace, and releases the lease
  (`-R`) when the VM stops. A static `address` is `A.B.C.D/NN,GATEWAY`, and the gateway is then the
  DNS server too. The LAN namespace's own `/etc/resolv.conf` is a bind mount of the lease's DNS, in
  its mount namespace only, which is what slirp4netns's resolver (the guest's 10.0.2.3) forwards to.
- **What the LAN sees.** One host at the lease's address with the VM's MAC: the guest's traffic, and
  mitmweb's upstream connections, leave through slirp4netns from that address. The TV-service ports
  (`layermeta.forwards`, with the VM's own `forward=` ports) are served there by a relay in the LAN namespace; `listen` does not apply.
  The same relay serves the inspector there, at `NETSTUB_WEB_PORT` (18083), behind the same
  password. The host itself cannot reach its own macvlan children, so the host-side relay keeps the
  same ports on `VM_ADDRESS`, and the inspector on `NETSTUB_WEB_HOST` (by default `VM_ADDRESS`).
  VNC (`beanjar vnc start`, on demand) is there too, at 5900: the relay joins it to `vnc.sock` in the
  socket directory, on which beanjar-vnc listens beside `VM_ADDRESS` ([vnc](vnc.md)). The guest stays
  on its private 10.0.2.0/24. Dedicated discovery helpers carry IPv4 SSDP and mDNS between that
  network and the LAN; this is protocol forwarding, not a general Ethernet broadcast bridge.
- **Hostname.** The DHCP request carries the VM's name as its hostname (option 12,
  `udhcpc -x hostname:NAME`; the manager passes `NET_LAN_HOSTNAME`), so a router that registers DHCP
  hostnames gives the VM a name in the LAN's own domain (`tv1` as `tv1.home.arpa` on a router whose domain
  is `lan`; the domain is the router's). The name is made a DNS label: lower-case, every character
  outside `a-z`, `0-9` and `-` (a name allows `.` and `_`) becomes `-`, none at either end, at most 63
  characters; two names that map to one label collide at the router. `beanjar info` shows it as
  `lan_hostname`. A VM renamed (`beanjar rename`) sends its new name from its next start. The
  checkout's own VM (`host/scripts/vm-launcher`) sends none.
- **Status.** The lease is in `run/lan.json` while the VM runs; `beanjar info` (`lan_address`),
  `beanjar list` (its ADDRESS column) and the GUI's summary (LAN address) show it. The inspector's
  URL is the LAN one: `beanjar info`'s `inspector.url`, with `inspector.local_url` for the host
  itself, and the GUI's Traffic Inspector, whose Copy URL copies the LAN URL and whose Open in
  Browser, the browser being on the host, opens the local one (shown as "On this host"). VNC's is
  shown the same way, while it runs: `beanjar info`'s `vnc.url` and `vnc.local_url`, and the
  GUI's VNC group.
- **Cleanup.** A stop releases the lease and deletes lan0. lan0 also belongs to the LAN namespace,
  and every process there is started with `PR_SET_PDEATHSIG`, so a killed wrapper takes the
  namespace, and lan0, with it. The kernel may keep such a namespace, and with it lan0 and the
  VM's MAC on the parent, until its last TCP sockets are torn down: measured on the Pi, about four
  minutes after a stop that left the deletion to the namespace. A macvlan cannot come up while
  another on the parent has its MAC, so a start waits up to a minute for that, then says why.
  `tests/integration/network/test_netns_lan.py` checks a stop and a kill, with a veth pair as the LAN.

## Ownership and interception

`host/core/beanjar/netns.py` owns the namespace and networking helpers. The `mitm` path
uses routing rather than netfilter. The proxy's inspector is available at the configured
host port (normally the VM's address, `127.0.1.<slot>:18083`, and with `link = "lan"` the VM's LAN
address too), behind a password made for each
start (or `BEANJAR_MITM_PASSWORD`), which the VM's owner-only `run/mitmweb.password` holds and the
manager shows ([inspect emulator traffic](../../user/inspect-emulator-traffic.md#the-password)).
`mode = "direct"` does not run the proxy.
`link = "off"` is the topology used when a run must have no guest network egress.

`mode = "mitm"` requests best-effort outbound capture. A missing proxy or failed
proxy startup leaves the routed guest network usable without interception. The
ingress interception rules are installed only after proxy readiness; if that
process exits, the rules are removed while the guest continues running. Existing
connections may fail and require a client retry. Starting a VM does not download
the optional proxy; `beanjar mitm setup --install` explicitly installs it.

TLS handshake failures also allow later connections to the same endpoint through
undecrypted. The failed connection is not replayed. These automatic bypasses are
bounded and preserve manual `ignore_hosts` rules. The inspector distinguishes
decrypted HTTP flows from passthrough TCP flows; it cannot capture traffic while
the proxy is unavailable.

## Discovery on LAN with MITM

`discovery.py` runs one helper beside `lan0` and one beside the guest TAP. They
exchange framed messages and fixed-guest service tunnels over a private Unix
socket. Namespace startup waits for each helper's readiness. Their lifetime is
the VM's, independently of mitmweb; the system `python3-dnspython` package supplies
the DNS codec without the optional proxy environment.

SSDP searches and mDNS queries cross in both directions. Temporary query sockets
correlate private replies with the requesting client; normal mDNS multicast,
legacy queries and QU replies keep distinct routing. Genuine advertisements and
goodbyes cross too. Multicast loopback controls, source checks and a bounded,
locked transmission cache prevent reflection. Guest-origin queries were checked
live against real LAN services, including Google Cast and Home Assistant.

Only verified guest advertisements and actual guest descriptions admit dynamic
TCP/UDP listeners. Each listener is ready before its advertisement is published,
and every backend is the fixed guest address. SSDP LOCATION and HTTP endpoint
references are translated to the LAN address. The HTTP proxy discovers DIAL's
application port from its actual `Application-URL` header. A registered SSDP
description is treated as bounded text even without a MIME type: queue's Gramlink
XML omits `Content-Type` and carries a plain `X_LGLINK-LOCATION` address. Other
untyped binary resources stream unchanged. HEAD responses preserve their empty
body without guessing the transformed GET length.

`discovery_dns.py` preserves compressed names, IDs, questions, QU/cache-flush bits,
record TTLs and service identities. Guest A records become the LAN IPv4 address;
guest AAAA records are omitted because this topology does not forward IPv6
services. A bounded host cache handles separate A and SRV packets. Actual SRV
TTLs govern listeners, positive refreshes cannot shorten an existing lifetime,
and a goodbye retires a port only after its other SSDP/mDNS references end.
Listener counts, clients, sessions, framed packets and rewritable bodies are
bounded; established service streams remain independent of discovery refreshes.

On the Pi's queue firmware, external native Windows probes received 44 SSDP
replies and five mDNS replies. All five advertised device descriptions returned
HTTP 200 with no private-address leaks; a normal Avahi browser resolved the
emulator's AirPlay, display, GHP and HAP services. AirPlay's read-only `/info`
returned HTTP 200, and SSAP accepted a WebSocket upgrade on port 3001. DIAL's
YouTube GET returned HTTP 200 with the stock-required YouTube Origin; an absent
Origin returned the stock 403. A separate LAN SSAP client then completed normal
PROMPT pairing, with consent accepted through the emulator's UI. Foreground-app
and audio queries succeeded; the advertised pointer-input WebSocket used the LAN
address on port 3001. RIGHT, HOME, BACK and volume buttons visibly changed the
Home screen selection, recent-items panel and volume overlay. No vendor
permission or application policy changed. The user's phone also listed the
emulator; its own pairing remains unverified. Media playback was not exercised.

## Host addresses and ports

Every VM has a slot N, and every host port of it is on its own loopback address, `127.0.1.N`
(Linux routes all of `127.0.0.0/8` to the loopback), at the TV's stock numbers. Several VMs run
side by side this way, and each is reached as a TV of its own, ares included, without port
arithmetic.

The layers' ports are the `forwards` of the selected layers' `layer.toml`
([layer metadata](firmware-targets.md#layer-metadata-layertoml)).

| Port on `127.0.1.N` | Guest | What | Override |
|---|---|---|---|
| 3000 | 3000 | SSAP, the second-screen API over WebSocket (`webos` layer: every webOS VM). The stock xinetd takes it from localhost only on most releases (`only_from = 127.0.0.1`; W21P has none), so a forwarded connection is closed there, as on a TV | `listen` (the address) |
| 3001 | 3001 | The same over TLS, open to every client: the second-screen clients' port. The certificate is the VM's own | `listen` (the address) |
| 6622 | 22 | Root SSH (`rootssh` layer, or Homebrew Channel's with `hbchannel`, whose `layer.toml` conflicts with `rootssh`); 22 is privileged on the host | `SSH_PORT` (the number); `listen` (the address) |
| 9922 | 9922 | Developer mode's jailed SSH, `prisoner` (`devmode` layer) | `listen` (the address) |
| 9991 | 9991 | Developer mode's key server, `ares-novacom --getkey` | `listen` (the address) |
| 18083 | (inside the namespace) | The `mitm` inspector (mitmweb), with a password (`run/mitmweb.password`) wherever it listens; with `link = "lan"` also on the LAN address | `NETSTUB_WEB_HOST`, `NETSTUB_WEB_PORT`, `BEANJAR_MITM_PASSWORD` |
| 5900 | none | The TV picture over VNC, [beanjar-vnc](vnc.md) (`beanjar vnc start NAME`), on the host, always with the VM's password (`identity/vnc.password`, `beanjar vnc password`); with `link = "lan"` also on the LAN address; a held port is refused with its holder named | `beanjar vnc NAME --port N` (with `lan`, this host's listener only); `--listen ADDRESS` (`nat` only); `beanjar vnc-password NAME --set` |

Homebrew Channel's telnet (guest port 23, unauthenticated root) has no host port: it stays inside
the guest.

- **A VM's own ports.** `forward` (`vm.toml`'s `network.forward`, the launcher's `FORWARD`)
  adds guest TCP ports of the VM's choosing, an app's server under test, to the same list:
  `beanjar settings NAME set forward=8080,3000`. `layermeta.forwards` appends them after the
  layers' (as layer `forward`), so they take every path the TV-service ports take: `direct`'s
  `hostfwd`, `mitm`'s `NETNS_FWD` and both relays, on `listen` with `nat` and on the LAN address as
  well as `VM_ADDRESS` with `lan`; `capacity.listen_conflict` checks them at a start like the others,
  `lan` VMs' too (on `VM_ADDRESS`, the host side of their relay). An item is `PORT` or `HOST:GUEST`
  (`8081:80` is guest 80 on host 8081, since the forward path carries both numbers anyway). Refused,
  when set and again at a start (a later change of the layers or `SSH_PORT` included): a host port a
  selected layer has (on webOS, 3000 and 3001 are the webos layer's), 5900 (VNC), 18083 (the
  inspector), one given twice, and one below 1024, which an unprivileged host process cannot bind.
  A guest port a layer forwards may be forwarded again from another host port. Settings change only
  while the VM is off, so a change applies at its next start; `unset forward` removes them all
  (none is the default). The ports carry no password of ours: whatever listens in the guest
  answers whoever reaches the address. On a `lan` VM a discovery listener (DIAL's port, say) on the
  same port as a `forward=` one cannot bind there.

- **Addresses.** `127.0.1` is the default prefix. A library's `config.toml` `addresses`
  (`beanjar library config set addresses=127.0.2`; three octets inside `127.0.0.0/8`, not `127.0.0`) gives its
  VMs, and those of its development libraries, `<prefix>.N` instead, so the VMs of two libraries
  run side by side. It changes only while every VM of the library is off. Everything derives
  the address from it: `keys.address`, the launcher's `ADDRESS_PREFIX` and so `VM_ADDRESS`, the
  forwards, the relay and the inspector, the `known_hosts` pins (rewritten at the VM's next start;
  no alias for the old address is kept), `beanjar info` and its ares profile, `list`, the GUI and
  `vm-launcher config ADDRESS`. The rest of this section says `127.0.1.N` for the default.
- **Slots.** A VM's slot is `network.slot` in its `vm.toml` (`vmidentity.ensure_slot`, `keys.address`),
  from 1 to 254. It is assigned when the VM is made: the lowest slot no VM of its slot domain
  holds, chosen under the domain's lock (`<library>/.slots.lock`) together with the rename that
  publishes the VM, so two VMs made at once never share one. It is kept while the VM exists and
  is not a setting. The slot domain is a library and the development libraries inside it
  (`<library>/development-*`: the checkout's VM and each `INSTANCE`, which regression uses), so a
  checkout VM never shares an address with a manager VM. A VM made before slots gets one once,
  written back, when its library is opened or it is started. A VM folder outside any library is
  alone in its domain (slot 1).
- **Both modes of `nat`.** `qemu.plan` turns `SLOT` (the launcher's variable from `vm.toml`) into
  `VM_ADDRESS`. `direct` forwards with QEMU's `hostfwd=tcp:127.0.1.N:PORT-:GUEST`; `mitm`'s
  host-side relay (`beanjar-relay`, `host/helpers/network/relay.c`) binds the same address and ports and bridges them into the
  namespace, the inspector included unless `NETSTUB_WEB_HOST` names another address. `link = "off"`
  forwards nothing, and a `lan` VM's ports will be on its own LAN address, not forwarded.
- **Reaching a VM.** Everything that says how to reach a VM uses its address: the launcher's
  notes, `beanjar info`'s `ssh` commands, `beanjar ares-TOOL -d NAME` (the ares tools
  with a device list of the VM alone: `127.0.1.N`, port 9922, user `prisoner`), `beanjar list`, the GUI's
  summary and `host/scripts/vm-launcher config ADDRESS [--dir VM]`. Each VM's `identity/known_hosts`
  pins its host keys as `[127.0.1.N]:6622` and `[127.0.1.N]:9922`, so two VMs' pins never
  overwrite each other.
- **The LAN.** The TV-service ports are one list, `layermeta.forwards` (the `forwards` of the
  selected and always-used layers, then the VM's own `forward=`; a new service port joins there). `vm.toml`'s `network.listen`
  (`link = "nat"` only; `beanjar settings NAME set listen=0.0.0.0`, the launcher's `LISTEN_ADDRESS`) moves them all to `0.0.0.0`
  or one of the host's addresses, for ares or a second-screen client on another PC; unset, they
  stay on `127.0.1.N`. They need no password of ours: the SSH servers take keys only, the key
  server's key is encrypted with the guest's passphrase, and SSAP pairs a client only when it is
  accepted on the TV's screen. With one address the `ssh` commands and `known_hosts` use it; with
  `0.0.0.0`, `127.0.1.N` still reaches them from the host. The ports stay the TV's, so
  `Library.start` (`capacity.listen_conflict`) refuses a start whose ports overlap a starting or
  running VM's (the same address, or `0.0.0.0` on either side), naming it, or a listener of the
  host's, naming its holder. The inspector and VNC keep their own addresses and password rules.

The library CA and per-firmware trust-store handling are described in [identity](identity.md).
Host interception alone does not establish that every guest TLS client trusts the CA; legacy
platforms have their own layer and trust-store boundaries. The
[traffic inspection guide](../../user/inspect-emulator-traffic.md) covers normal use and
troubleshooting.

## Identity and validation

MAC, serial and model-key provisioning belong to VM identity, not the network mode. A synthetic
identity is not a valid LG server credential.

Regression with `NET_LINK=off` captures the guest NIC, validates the pcap and checks it for recognized
LG contacts after the VM stops. An empty valid capture is consistent with the link-down
topology. Network capture results do not establish application TLS or authentication success.
