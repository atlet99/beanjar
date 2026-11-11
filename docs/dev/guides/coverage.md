# Coverage of the host programs

A build directory of its own, `build/coverage`, builds the host programs with gcov, runs the
host unit tests, and reports per file how many lines and branches they ran: the C programs
through gcovr (or lcov), the VM manager (`host/core/beanjar`, `host/ui/beanjar_ui`) through coverage.py. It runs on
the x86 PC; no VM, firmware or Pi is involved.

## Running it

Debian packages: `gcovr`, `lcov`, `python3-coverage` (on top of the host build's).

```sh
meson setup build/coverage -Db_coverage=true -Doptimization=0 -Ddebug=true \
  -Dpublish_dir=$PWD/build/coverage/publish -Dfirmware_cache=disabled
meson compile -C build/coverage
meson test -C build/coverage                   # all host suites, in parallel (about 90 s)
ninja -C build/coverage coverage-report        # the report below
```

`-Dpublish_dir` keeps the instrumented programs out of `build/`, where the checkout's VMs take
theirs. `-Doptimization=0` keeps lines and branches as written. For a fresh measurement, remove
the counters of the earlier runs first, since gcov and coverage.py add to what is there:

```sh
find build/coverage -name '*.gcda' -delete
rm -rf build/coverage/meson-logs/coverage-python
```

`coverage-report` writes `build/coverage/meson-logs/coverage-report/`:

- `summary.txt`: per file, lines and branches run, with totals per directory. It is also printed.
- `c/index.html`: gcovr's per-line HTML of the C files.
- `python/index.html`: coverage.py's HTML of the manager.

Meson's own `ninja -C build/coverage coverage` still works for the C side. It writes
`coverage.txt`, `coverage.xml` and the lcov HTML `coveragereport/` into `meson-logs/`.
`meson/coverage/gcovr.cfg` limits the report to `host/`; `coverage-report` passes it to gcovr with
`--config`. Meson itself only reads `gcovr.cfg` and `.lcovrc` from the source root, so its own
`coverage` target does not apply them: it also lists the tests' checkers and the generated files.
`meson/coverage/lcovrc` is the equivalent for lcov (`--config-file`).

## What it measures

- **C:** `beanjar-relay` and `beanjar-lan-helper` (`host/helpers/network`); `media-helper` and
  `host/media`; the viewer core `libbeanjar-viewer-core.so` and `viewer-headless`
  (`host/viewer/core`); `beanjar-vnc` (`host/vnc`). `kvm-haltpoll` is aarch64 only and is not
  built here. gcovr counts a branch as covered when it is taken.
- **Python:** `host/core/beanjar` and `host/ui/beanjar_ui`, in every Python process of the tests. That includes the
  CLI, the launcher, the units and netns, which the tests start as their own processes. The
  test setup (`meson/coverage/meson.build`, the default setup of a `-Db_coverage=true` build)
  puts `meson/coverage/sitecustomize.py` on `PYTHONPATH` and sets `COVERAGE_PROCESS_START` to
  the build's `coveragerc`. Each process writes a parallel data file, and `coverage-report`
  combines them. A process started with `-I` or `-S`, or with a `PYTHONPATH` that drops the
  inherited one, is not measured.

## Stopping processes so their counters survive

gcov writes its counters at exit, and coverage.py writes at exit or on SIGTERM
(`sigterm = true`). A process ended with SIGKILL writes neither. Tests therefore stop what they
started with SIGTERM, and use SIGKILL only after a timeout. `testunits.end()` (`tests/lib/testunits.py`) does this
for the manager tests. Before `test_relay.py` did, relay.c read 84 % of lines instead of 95 %.

`media-helper` has no SIGTERM handler, so the default action would end it without writing
anything. A coverage build links `meson/coverage/gcov-flush.c` into it. That file blocks SIGTERM
and SIGINT before `main` and waits for them in a thread of its own, which writes the counters
and ends the process with the signal. It cannot be a signal handler: gcov takes locks a decoder
thread may hold, and a first version done that way hung. `beanjar-relay` (signalfd),
`beanjar-vnc` and `viewer-headless` exit cleanly on SIGTERM by themselves.

The netns tests run the build's own relay (`BEANJAR_RELAY`, `paths.Tree.relay`), not the one
published to `build/network/`.

## First measurement (2026-10-13)

The whole host suite, 46 tests passed and 2 skipped (packaging checks that need a published
tree). Lines and branches run:

| Program | Lines | Branches |
|---|---|---|
| beanjar-relay | 94.9 % (240/253) | 67.2 % (164/244) |
| beanjar-lan-helper | 0 % (0/97) | 0 % (0/84) |
| media-helper + host/media | 78.4 % (1034/1319) | 52.1 % (771/1479) |
| viewer core (libbeanjar-viewer-core) | 47.9 % (904/1887) | 28.0 % (490/1752) |
| viewer-headless | 49.4 % (76/154) | 31.1 % (76/244) |
| beanjar-vnc | 92.6 % (313/338) | 64.7 % (238/368) |
| Python manager (beanjar) | 84.7 % (7249/8554) | 78.5 % (2761/3519) |

The weakest spots:

- **`beanjar-lan-helper`:** no test runs it. `test_netns_lan.py` uses a stand-in through
  `BEANJAR_LAN_HELPER`, because the real helper needs CAP_NET_ADMIN.
- **Viewer core, `audio.c`:** 6 %. The D-Bus audio listener (`registered`, `on_init`,
  `on_write`) never runs, because the stand-in QEMU offers no audio.
- **Viewer core, `media.c`, `media-ipc.c` and `media-frame.c`:** 15 %, 0 % and 0 %. The viewer's
  video path is not run: `reader`, `media_update`, `draw_stream`, `import_drm`, `media_close`.
  `check-media-mix` drives only the mixer.
- **Viewer core, other functions:** `video.c` is at 38 %, and `core.c`'s
  `capture_render_frame`, `scanout_dma` and `scanout_map` never run.
- **`viewer-headless`:** `load_replay` and the evdev input never run.
- **host/media:** `codec.c` 54 % (the hardware decoder path: `hw_device`, `get_format`),
  `export.c` 66 % (`export_frame`), and the 10-bit conversions in `frame.c` (`p010_to_nv12`,
  `to_8bit`).
- **Python:** `lls.py` 44 % (`generate`, `generate_nyx`, `extract`, `tvconfig`), `launch.py`
  65 % (`run`, `qmp`, half of `main`), `mitmsetup.py` 64 % (`_run`), `ui.py` 70 % and
  `netns.py` 73 % (`off`, part of `lan`).
