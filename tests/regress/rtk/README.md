# RTK counter label lifetime

`check-v4l2-counter-lifetime.c` exercises the lifetime of the ioctl counter labels after a V4L2 close using two V4L2 scaler nodes
actually named in the loaded RTK module's `v4l2_vsc` parameter. In an isolated
RTK guest, build and run it as follows (the current K6/K7 configuration uses
`video30,video31`):

The example uses an already-running isolated checkout instance named `rtk-probe`;
use the instance that was explicitly started for this test, with the selected
K6/K7 firmware and module. On that instance's VM host:

```sh
mkdir -p build/tests/rtk
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror \
  tests/regress/rtk/check-v4l2-counter-lifetime.c -o build/tests/rtk/check-v4l2-counter-lifetime
INSTANCE=rtk-probe host/scripts/vm-launcher put build/tests/rtk/check-v4l2-counter-lifetime
```

These host commands address that instance's **outer guest shell**, verify its
loaded node configuration and counter file, and run the probe:

```sh
INSTANCE=rtk-probe host/scripts/vm-launcher console 'cat /sys/module/rtk/parameters/v4l2_vsc'
INSTANCE=rtk-probe host/scripts/vm-launcher console 'ls -l /dev/video30 /dev/video31'
INSTANCE=rtk-probe host/scripts/vm-launcher console 'cat /proc/rtk/ioctl'
INSTANCE=rtk-probe host/scripts/vm-launcher console \
  '/host/build/tests/rtk/check-v4l2-counter-lifetime /dev/video30 /dev/video31 256' 120
```

For a directory-managed isolated VM, use `host/scripts/vm-launcher put --dir VM_DIR
build/tests/rtk/check-v4l2-counter-lifetime` and add `--dir VM_DIR` after `console`
in every console command instead of selecting `INSTANCE`. Neither example selects
the default checkout VM. Use the two actual configured node paths if they differ.

The probe issues only public `VIDIOC_QUERYCAP`. The current stand-in's
`rtk_vsc_ioctl()` returns zero without filling capabilities or changing scaler
state for this request; the probe assumes no capability contents or vendor reply.
It closes each queried file, allocates sixteen per-open records on the other node,
and checks `/proc/rtk/ioctl` both while those records exist and after closing them.
It requires each original label to retain at least its baseline count plus this
probe's calls, allowing concurrent stock requests, and rejects duplicate matching
rows or incomplete counter reads. Missing nodes or counters fail explicitly.

`node_ioctl()` must pass module-lifetime names to retained counters, while
`rtk_vsc_open()`/`rtk_vsc_release()` continue allocating/freeing per-open state.
Alternating nodes makes reused per-open storage carry a different name and can
expose the original dangling-label bug. The runtime result must be measured on
both the original and corrected module; compilation alone does not prove the fix.
