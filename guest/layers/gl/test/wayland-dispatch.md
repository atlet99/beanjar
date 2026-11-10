# Wayland dispatch compatibility regression

Run `guest/layers/gl/test/wayland-dispatch.sh` after the guest build (`scripts/build.sh guest`) has
fetched the Wayland archive into `subprojects/packagecache`. It builds a native library and tests it
without a compositor or VM. Native build dependencies are a C compiler, libffi and expat
headers, Meson, and Ninja. `BASELINE=1 guest/layers/gl/test/wayland-dispatch.sh` tests the unpatched
library: the first case should hit the five-second alarm and fail.

The wire peer sends only a callback event for another queue, without a `wl_display.delete_id`
event. QtWayland 5.0 expects an untimed dispatch to return zero in that case; Wayland 1.26 waits
for more input. Tests also cover pending events, a blocking initial read, both timeout APIs
(including NULL timeouts), timeout cancellation, and roundtrips. Callbacks stand in for the
EGL queue's buffer-release event that triggered the WAM deadlock.

To run the same test against the generated GL layer, cross-compile it on the SDK host:

```sh
/opt/arm-webos-linux-gnueabi_sdk-buildroot/bin/arm-webos-linux-gnueabi-gcc \
  -std=c99 -Wall -Wextra -Werror -pthread \
  -Ibuild/guest/layers/gl/mesa/stage/usr/include guest/layers/gl/test/wayland-dispatch.c \
  -Lbuild/guest/layers/gl/mesa/stage/usr/lib -Wl,-rpath-link,build/guest/layers/gl/mesa/stage/usr/lib \
  -lwayland-client -o build/wayland-dispatch-arm
```

Copy the binary to the VM host's shared tree and run it inside `webos-enter` with the GL layer
loaded. It dynamically links the guest's production Wayland client. The test creates only
socket pairs and does not connect to the compositor.

For the firmware integration check, launch Settings, wait 18 seconds after the launch reply,
then launch `com.webos.app.tvuserguide`. Confirm its full menu renders and
`com.palm.webappmanager/listRunningApps` still answers. Repeat transitions and test a fresh
launch: this firmware's `com.webos.applicationManager/close` takes `processId`, obtained from
the launch response. Also run the Normal and fresh-state FirstUse `guest/scripts/check-m1.sh` checks
and `tests/regress/video/check-video-app.sh` with the rebuilt GL layer.
