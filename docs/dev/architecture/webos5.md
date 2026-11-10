# Systemd-era webOS targets

The supported systemd-era targets are W20L `k6lp-jhericurl` (webOS 5) and W21P
`k7lp-kisscurl` (webOS 6). Later release families are not additional
supported targets. `targets.MODELS` remains the authority for import support.

## Init and service boundaries

Stock systemd runs as PID 1 inside the existing firmware namespace. Compatibility handling
maps the old Upstart-facing events to the release's systemd targets. Platform-specific
overrides use the systemd form; the shared emulator kernel and outer initramfs are unchanged
by the guest's choice of init system.

`legacy-broadcast` replaces the old tvservice process layout, and `tvservicemock` serves
`tv.systemproperty`. Regression follows the release's actual service names and broadcast
variant. A missing or unsupported `instantBooting` response produces a standby skip rather
than an invented Quick Start+ capability.

## Board state and storage

The release is identified by its codename, not its version prefix or model year. Board
configuration is interpreted using the supplied firmware's own option tables. The retail
profiles recorded for W20L and W21P are distinct and do not borrow each other's regional values.

Lowlevelstorage owns the EEPROM records. A fresh state is initialized through the firmware's
factory path with the selected board configuration; the host does not invent an opaque LLS
record image. The [partition map](partitions.md) exposes the partitions that the stock storage
clients require. FirstUse completion also includes the settings records that the stock
database health check expects, not just a marker file.

## Graphics and evidence

The stock surface manager and Qt starfish platform use the emulator's Mesa boundary and
framebuffer interfaces. Secondary framebuffer requirements belong to the board configuration.
The [viewer](viewer.md) presents the resulting display through the same host path as the
earlier webOS targets.

Current build and regression
commands are in the [build guide](../guides/build.md).
