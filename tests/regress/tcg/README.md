# x86 TCG performance checks

These tests distinguish ARM instruction translation, memory access and guest-kernel costs
from boot-manager timeouts. They run sequentially with private Unix sockets, `STATE=none`,
`link=off`, 2 GiB RAM and the existing guest/kernel/initramfs. Every VM is terminated in a
`finally` block. No Pi connection or persistent guest disk is used.

Build with the SDK and read-only firmware libraries (the guest build; binaries remain under `build/`):

```sh
sh tests/regress/tcg/build.sh
python3 tests/regress/tcg/run.py baseline nopauth lean one two cache512
python3 tests/regress/tcg/run.py baseline nopauth baseline nopauth   # repeats, interleaved
python3 tests/regress/tcg/run.py --services baseline nopauth lean
```

The microbenchmark runs without Upstart and repeats each workload three times. It checks
integer and memory checksums. `--services` starts Normal/headless webOS, requires M1 PASS,
then times three groups of 30 independent `luna-send` calls to tvpower. This includes
process startup, library loading, IPC and service handling, not just the service's execution
(LG's tvpowerd since 2026-09-27; earlier numbers timed the tvpower stub).
It also times three groups of 1,000 sequential requests over a persistent LS2 connection,
after 10 warmup calls per group. The test-only `tcg-perf` layer grants that client access
to tvpower without disabling hub security. It does not measure UI, JavaScript or multicore
throughput. Keep other CPU workloads idle.

Results and serial/webOS logs go into `build/tcg-perf/<timestamp>/<profile>/`, which is also the
VM's `RUN_DIR`: the guest's shares are its `share/`, and the run puts the scripts and programs the
guest runs there (`host/scripts/vm-launcher put`). Boot-to-shell
is host wall time; workload times use the guest monotonic clock or uptime (no `icount`).
Repeat profiles in separate invocations to compare across fresh boots. The SDK binary can
also be run with `qemu-arm build/tcg-perf/perf-arm` to compare user-mode translation, but that
is not a full webOS compatibility test.

The launcher accepts `TCG_CPU`, `TCG_THREADS` and `TCG_TB_SIZE`, in addition to `SMP`.
Use those instead of appending another `-accel` through `QEMU_EXTRA`: multiple accelerators
form a fallback list, so a second TCG entry does not reliably change the active one.
`TCG_TB_SIZE` is MiB; an explicit size is not necessarily larger than QEMU's default.

`nopauth` disables pointer authentication only; it is the launcher's default since 2026-09-26
(`baseline` sets `max` explicitly). `impdef` selects QEMU's implementation-defined PAC
algorithm, which QEMU 10 already uses by default for `max`. `nosve` disables SVE and SME only.
`lean` combines both; `one-lean` also uses one vCPU. Disabling pointer authentication removes
the guest kernel's PAC checks. It does not change the host or the Pi launcher.
`serial` keeps four guest CPUs but uses single-thread TCG. All profiles retain normal
instruction and memory emulation; none is a reduced-accuracy mode.

## Storage comparison

```sh
sh tests/regress/tcg/build.sh --io
python3 tests/regress/tcg/run.py --io baseline
```

With `ROOTFS=uncompressed` in the environment, the SquashFS side is the uncompressed copy from
`host/scripts/vm-launcher firmware-cache` instead of LG's LZO image.

This creates a 64 MiB scratch ext4 image containing the firmware's 25 MB WebKit library,
attaches it read-only, and compares it with the same file in the stock SquashFS. Three
cold/warm pairs use guest-only `drop_caches`; host caches are left intact. No Upstart runs
and no persistent state is attached. The fixture size check is specific to WT1H. The test
measures sequential reads, not metadata, writes or whole app launches. Firmware-derived
files remain under ignored `build/`. Do not rebuild fixtures while a benchmark is running.
