# Creating and running VMs

Each VM is one TV: its own firmware, settings, state disks and identity, in the library. Make
and run them from the manager window or the CLI; both work on the same library, and the window
follows what the CLI changes. `beanjar --help` and `beanjar COMMAND --help` describe every
command, and the [manager guide](../../host/core/README.md) has the details.

## The manager window

`beanjar-ui` (from a checkout, `python3 -m beanjar_ui.manager`) lists the VMs with their state.
**New VM** makes one from a `.epk` or a directory of extracted paks, with pages for its hardware,
display and audio, network and layers. **Start**, **Shut Down…** and **Connect** (the viewer) are
in the header bar, and the VM menu has Settings, Wipe, New Identity, Rename, Open Log Folder and
Delete. The Summary shows each VM's firmware, address, connections and identity. Closing the window
or a viewer never stops a VM.

## From the CLI

```sh
beanjar create tv1 --firmware /path/to/extracted-paks --import
beanjar start tv1
beanjar viewer open tv1 --remote
beanjar list --local
beanjar stop tv1
```

- `create` defaults to the firmware model's RAM, 4 CPUs, the `normal` and `gl` layers, the NAT
  network through the traffic inspector, and the D-Bus display with sound. `--mem`, `--smp`,
  `--layers`, `--link`, `--mode`, `--display` and `--audio` change them.
- A firmware whose target has never booted (an untested target, see the
  [support table](../../README.md#supported-firmware)) is accepted with a warning, "not tested yet:
  may not boot", when the VM is created and each time it starts; New VM marks it the same way.
- `start` returns at the guest's initramfs shell, and the TV boots on to Home in the background.
  Several VMs run at once as far as the host's memory allows (`beanjar host config set
  max_running=N` caps them).
- `list --local` shows this machine's VMs and their state; `info NAME` one VM's settings, address,
  ports and SSH command.
- `stop` saves the guest's logs and sends the TV's own power-off; a guest that cannot take it is
  synced and stopped instead. `stop --unplug` cuts the power at once and may lose data.
- `wipe NAME` blanks the state disks (a factory-fresh TV, first-use setup included unless the
  `normal` layer skips it); `delete NAME` removes the VM.
- `identity renew NAME` makes the VM another TV (new MAC, serial and keys, state wiped);
  `clone SRC DEST` copies a VM as another TV with its state kept.

Every command takes `--json` for scripts.

## Settings

`beanjar settings NAME` shows a VM's settings, and `settings NAME set KEY=VALUE` or `unset KEY`
changes them while it is off (`beanjar settings --help` lists every key):

```sh
beanjar settings tv1 set mem=1536 layers=normal,gl,devmode
beanjar settings tv1 unset broadcast
```

The layers add to the stock firmware; the first listed wins where two have the same file:

| Layer | What it does |
|---|---|
| `normal` | Skips the first-use wizard |
| `gl` | Graphics through Mesa (virgl) under the stock compositor |
| `headless` | No graphics; instead of `gl` |
| `devmode` | LG Developer Mode and its SSH, for the webOS SDK tools ([apps](apps.md)) |
| `rootssh` | Root SSH with the VM's keys on port 6622 |
| `hbchannel` | Homebrew Channel, as on a rooted TV; needs `devmode`, instead of `rootssh` |

A global firmware boots as a US model where one exists; to boot it as another region's model, see
[Boot a global firmware as another region's model](board-profiles.md).

## Network

Two settings decide a VM's network ([guest networking](../dev/architecture/network.md)):

- `link`: `nat` (the default, behind this host), `lan` (on the host's wired LAN with its own MAC
  and DHCP lease, after `beanjar library config set lan_parent=eth0`) or `off`.
- `mode`: `mitm` (the default), which records the guest's web traffic in a traffic inspector, or
  `direct`. See [Inspect emulator traffic](inspect-emulator-traffic.md).

Each VM has its own loopback address, `127.0.1.N`, and its services are on the TV's own port
numbers there: SSAP 3000 and 3001, root SSH 6622, Developer Mode 9922 and its key server 9991, the
traffic inspector 18083 and VNC 5900. `beanjar info NAME` prints them. To reach them from another
machine, `settings NAME set listen=0.0.0.0` (with `link=nat`), or use `link=lan`.
`settings NAME set forward=8080` relays an app's own guest port as well.

## Identity

Each VM has its own MAC, serial and keys, made when it is created. Without a model key of your own
it has a synthetic one, which LG's servers do not accept. If you have the model key of a matching
TV, install it while the VM is off:

```sh
beanjar key model tv1 install /path/to/model-key
```

Secure-store items beyond the model key go in the VM's `identity/sestore.toml`; see
[Configure secure-store items](sestore.md) and [per-VM identity](../dev/architecture/identity.md).

## Other machines

A VM on another host is `HOST/NAME` in every command once the host is added with
`beanjar host add NAME ssh://[USER@]HOST`; `beanjar list` then asks every host. See
[Several machines](../../host/core/README.md#several-machines).
