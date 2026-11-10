# O22 UCOM transport probe

`check-ucom.c` checks the ARM32 compatibility path against the actual loaded
O22 UCOM driver. The request layout and 24-byte payload bound come from LG's
webOS22 O2.0 `include/ucom_kapi.h` and `chip/o22/ucom` sources, with ioctl
values also checked against the stock O22 KADP client.

Build with the target ARM32 SDK compiler, using `-Wall -Wextra -Werror`, then
stage the binary in an isolated packaged O22 VM's exported `share/out/diag`
directory. Run `/mnt/host/out/diag/check-ucom` inside the firmware namespace.
Remove the staged binary after the test. It requires ARM32 userspace and
returns 77 otherwise; an ARM64 run cannot validate compat pointer handling.

The probe initializes the command transport and reads power state/reason. It
prints those bytes as observations rather than asserting invented hardware
values. It separately checks the poisoned upper pointer word, oversized
payload rejection, invalid write buffer, unsupported ioctl, and preservation
of the caller's buffer on unsupported micom reads. Invalid writes must fail
before reaching the virtual micom. The unsupported command is derived from
`guest/kernel/modules/lgemu/i2c.c`'s `micom_answer` default branch.

A successful probe alone is insufficient for native input validation. Query
stock `com.webos.service.tvpower/power2/getPowerState`, inspect boot completion
and service restart counts, then use the packaged viewer to dismiss the
native browser's first-run prompts. Those checks distinguish a working
transport from a stock power transition and delivered client clicks.

## O22N2 scaler probe

`check-vsc.c` exercises the ARM32 V4L2 scaler ABI measured from stock O22N2
ombre libval and LG's supplied `videodev2-ext.h`. Build with the target ARM32
SDK compiler using `-Wall -Wextra -Werror`. Stage it in the isolated VM's
exported `share/out/` directory and run it inside the firmware namespace.
It changes both windows, so close the owned test app and stop `videooutputd`
and `avmutemanager` first; restart those services afterward and remove the
staged binary. A stock service may need termination if its stop hangs waiting
for its event worker; keep this confined to the isolated trial.

The probe checks readback, shared state across opens, independent windows and
event queues, readiness and nonblocking dequeue, real blank/unblank behavior,
VDO routing and disconnect, and error paths that preserve existing state.
It drains the retained `/dev/lgemu-video` events to check the actual current
transport table: a window connects only when the VDO route matches its VDEC
resource. Changing the route or disconnecting it must stop presentation.
Black-bar detector queries, cropping, rotation and frame readback must fail because those
features are absent. This probe complements ordinary opaque-app playback,
pause/seek captures and recorded audio; it does not qualify other codecs or
hardware image processing.

## O22N2 capture probe

`check-capture.c` checks the measured ARM32 libvtcapture MMAP sequence and
negative requests. Build with the target ARM32 SDK compiler using
`-static -Wall -Wextra -Werror`, stage the binary in the isolated VM's exported
`share/out/`, and run it inside the firmware namespace. It does not change
scaler routing or require stopping videooutputd.

Run `--present [output.nv12]` with an active viewer showing a nonuniform scene,
then `--absent` after closing it. `--disconnect` prints a readiness message
after STREAMON and waits for a newline: disconnect the viewer before sending
that newline. It requires failure without completing any buffer. A provider
that announces presence but does not reply exercises the bounded timeout.
The optional artifact is 320×180 NV12 and must be inspected. Remove the staged
binary and artifacts afterward.

The probe checks both standard NV12M/NV16M formats and LG's private format
enums, actual full-height NV16 capture, freeze/unfreeze state, queue state,
fresh sequence increments, unsupported controls and
retained mappings across buffer replacement and file close. Native
`com.webos.service.capture/executeOneShot` requests and retrieved PNGs qualify
the stock service separately. With an active video window, repeat native
capture using a viewer started with `--no-media`: the displayed checkerboard
must produce an error and no image. Reconnect a decoding viewer and verify
capture succeeds again.
