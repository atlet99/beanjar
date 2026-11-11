# Video and media transport

The emulator keeps LG's media services and pipeline control above the vendor decoder/sink
boundary. Emulator elements send compressed elementary streams to the host for decoding.
The [media stand-in contract](media-standins.md) describes that boundary.

## Guest boundary

The SoC layer provides GStreamer elements under the vendor factory names. LG's `libpf`,
`lpsink`, `decproxy` and umediaserver select and control them through the expected caps,
properties, signals and bus messages. Front ends share the `mediaport`/`streaminfo` transport.
The cut is before vendor media kernel interfaces such as liblxv, MStar Utopia and Realtek RPC.


## Transport and session ownership

`include/webos-media.h` defines the media stream protocol. Messages carry session opening,
caps, timestamped buffers, playing/paused state, flush, EOS and close. Caps select the stream's
codec and kind; an element's name alone is insufficient because a video element may also
receive audio. `include/webos-video.h` defines the video-window protocol.

The VM's `host/helpers/media-helper` sidecar owns the guest's `webos.video` and `webos.media.<n>`
ports and the compressed video/audio decoders. It keeps decoding and retiring due output without
a viewer, retaining a real current picture and bounded future output. A late or relaunched
viewer receives that picture, including while paused, without restarting decode midway through
a GOP.

The helper serves decoded media over versioned local `SOCK_SEQPACKET` sockets
(`include/host-media.h`). Descriptors and their `SCM_RIGHTS` FDs carry native software planes in
sealed memfds, real DRM_PRIME objects/layers/planes, or 48 kHz float-stereo PCM. The viewer owns
presentation scheduling, GL composition and PCM playback. The video-window table remains on
the separate stream socket.

One viewer can own each helper socket; a second connection is refused. Viewer backpressure
uses a bounded lease budget and discards complete decoded outputs. A genuine compressed-ingress
overload explicitly resets/reconnects the guest port for session replay and a new keyframe.
`media-sessions.log` records guest lifecycle, `media-helper.out` records decoder work, and the
viewer records presentation. `MEDIAHUB=0` omits the helper: decoding is off, and the launch's
`vm-info.toml` says `media = "off"` so that every viewer core of the VM runs as with `--no-media`;
there is no compressed decoder in the viewer. The helper decodes with the private FFmpeg
(beanjar-vendor's, with the V4L2 Request API hwaccels; docs/dev/guides/build.md, "The prebuilt
programs").

A guest decoder whose output goes to a consumer that needs pixels (media-standins.md, "Decoding
for the guest") opens a DECODE session instead. The helper decodes it (hardware first) and
answers each input on the same port with PICTURE messages in NV12 or YUY2, the last one marked
LAST; no viewer sees the session. The pictures go into the VM's media pool when it has one.
The pool is a 128 MiB file `media-pool` in the VM unit's RuntimeDirectory (a tmpfs that
systemd removes with the unit, after a crash or SIGKILL too), shared through QEMU's stock
`ivshmem-doorbell` device; the media helper is the device's ivshmem server and owns the
allocator. A session gets extents of 64 KiB granules (a control extent with its two rings, and
picture slots, by default four), and the helper punches an extent out of the file
(`FALLOCATE_FL_PUNCH_HOLE`) once the session ended and the guest no longer maps it, so an idle
VM costs nothing. Pictures and slot releases go over rings in the control extent, with one
MSI-X vector each way that rings only while the other side waits; virtio-serial keeps OPEN,
CLOSE and the control messages. Everything the guest writes into the pool is checked on the
host against the helper's own copy of the extents. If no slot comes free within 100 ms the
picture is dropped and counted (`slot-drops`), and the LAST answer still comes; if the pool is
full the session's pictures come inline from then on. A launch without a unit (the setsid
fallback) has no device, and its pictures come inline in the PICTURE messages. The guest side
is the built-in `webos-ivshmem` driver (kernel patch 0018) and the `media-pool` module.

The decoded transport and lifetime contract: generation fences, immutable memory, GPU-complete releases and the bounded quarantine
plus permanent download fallback after an unclean hardware disconnect.

## Clock and decoding

The guest pipeline clock is the master. Stream state carries the base time and paused running
time; headers carry the guest monotonic and pipeline clocks. The helper maintains the minimum
receipt offset and sends clock snapshots to the viewer. Helper output retirement and viewer
picture/audio scheduling use that mapping; viewer receipt delay is not a new clock offset.
Pause freezes presentation, and flush clears queued output and pauses timing until new state
arrives. Audio-device drift is corrected in viewer playback.

FFmpeg video/audio decoding and media-audio resampling run in the helper. `MEDIA_HWDEC` selects
its initial hardware policy; viewer `--hwdec` is acknowledged for the next codec-open boundary,
without changing an active decoder. The viewer imports supported DRM PRIME pictures as EGL
images or uploads native software planes. Genuine hardware downloads and required GLES2
10-bit reduction happen in the helper. Historical hardware measurements are specific to their
host, codec and implementation; software/ownership tests do not qualify GPU synchronization.

## Display and audio

LG's window control supplies placement and visibility. The viewer combines the decoded video
with the TV's OSD. A video plane without input is black, and so is a shown window without a
decoded picture while decoding is on: before the first frame, between streams, after a helper
restart, and for a codec the helper has no decoder for (as on a TV). Only with decoding off
(viewer `--no-media` or a `MEDIAHUB=0` launch) does such a
window show the diagnostic checkerboard (60 guest pixels per square, light grey and dark blue),
which marks "video here, not decoded". Either way the window counts as drawn without a picture
(`video_draws` and `video_pictures` in the viewer's exit line), and VIDEO capture refuses it. Guest PCM delivered through QEMU's
D-Bus audio and host-decoded media audio are distinct paths; see [audio](viewer-audio.md).
The shared viewer core serves the GTK window and headless checks.

## Validation and scope

The [TV feature suite](../guides/build.md#tv-feature-suite) checks decode cases separately
from boot/GUI smoke tests. A boot pass or a screenshot does not establish media playback.
The helper has real decode/reconnect and lifecycle tests in `tests/integration/media-helper/test_media_helper.py`;
FD/seal and lease ownership fixtures are in the same directory.

Protected media is outside the implemented boundary. Supported factory names and advertised
caps do not imply every codec, native-app route, trick-play mode or secure-buffer path has
been exercised. Research records distinguish observed results from unverified interfaces.
