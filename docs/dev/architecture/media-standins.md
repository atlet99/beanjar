# Media stand-ins

The emulator replaces vendor GStreamer decoder and sink elements while retaining LG's
pipeline services. These elements hand compressed media to the common host transport;
[video](video.md) describes session ownership, timing and presentation.

## Replacement boundary

| Family | Targets | Element boundary |
|---|---|---|
| LG115x | `h13-afro`, `m14-beehive` | `vdecsink`, `adecsink` |
| LG1313 | `m16p-dreadlocks2` | `omx_lxvideodec` / `lxvideodec`, `omx_lxvideosink` / `lxvideosink`, `audio_dec`, `audio_sink` |
| MStar | `m2-dreadlocks`, `m3-goldilocks` | OMX decoder factories, `mvsink`, `masink`, `omxaudiosink` |
| Realtek | `k5lp-goldilocks2`, `k6lp-jhericurl`, `k7lp-kisscurl` | OMX decoder factories, `dvovideosink`, `rtkalsasink`; `rvscale` where the firmware has it (K7, K8) |

`libpf`, `lpsink`, `decproxy` and umediaserver stay stock. A stand-in must be discoverable through
the same factory names, ranks, caps and class metadata that those clients use. The actual
factory set is selected by the target's build; the table is not an assertion that every stock
plugin or secure variant is replaced.

## Control and timing contract

- Preserve the properties and event shapes the stock pipeline reads and writes, including
  boxed `resource-info`. Resource assignment is distinct from successful decoding.
- Emit the video-information message understood by that release. Older clients use
  `video-info`; later clients use `media-info`. Source placement matters where libpf drops
  messages originating under `decproxy`.
- Retain normal GStreamer preroll, state, flush, EOS and query behavior. `ASYNC_DONE` participates
  in the stock load/seek completion path; completing a method call alone is insufficient.
- Preserve a single pipeline clock owner and the applicable audio context. Forward timestamps
  and state to the shared media transport rather than inventing an independent video clock.
- Underrun signals and frame counters must reflect their implemented semantics; exposing a
  property is not evidence that a hardware event was reproduced.

These are compatibility obligations at the selected boundary. The
per-family factories, types, caps and messages are in the sources named below. They describe what stock clients expect, which must be
distinguished from what each stand-in currently implements.

## Decoding for the guest

A stand-in decoder normally passes the compressed stream through to our own sink, which sends it
to the host. Some pipelines end in a consumer that needs real pixels instead: LG's camera player
(`libStarfishCameraPlayer`) decodes the camera's MJPEG with `appsrc ! omxmjpegdec ! ... ! appsink`
and hands the appsink's buffers to Chromium. For those the decoder asks the host to decode
and pushes the pictures it gets back (`include/webos-media.h`, "Decoding for the guest").

- **Which consumer needs pixels.** On the first buffer the decoder walks downstream from its src
  pad: through ghost and proxy pads and the src pads of elements in between, to every sink that is
  reached. If every one of them is ours (a type whose ancestry is `EsSink-*`, essink.c), the
  passthrough stays. If any other sink is reached (an element flagged as a sink, or one without
  src pads), the decoder decodes on the host. A missing link leaves the passthrough and asks
  again on the next buffer. Only video decoders do this.
- **The format.** The decoder takes the formats of its own src template (system memory first),
  keeps NV12 and YUY2, and picks the first that downstream accepts. N21D's camera player gets
  YUY2; W22H's capsfilter asks for NV12.
- **The layout.** Each picture carries a GstVideoMeta with the plane offsets and strides. The
  camera player's new-sample handler reads plane 1 only from that meta, and without it the
  chroma is garbage. libgstvideo is linked from the firmware; the layer compiles against
  GStreamer core's headers only, so the two functions are declared locally.
- **Memory.** On a VM run as a systemd unit the pictures sit in the VM's media pool (the
  transport is in video.md, "Decoding for the guest", and `include/webos-media.h`, "The pool").
  The decoder claims its port on `/dev/lg/webos.media-pool` (the `media-pool` module), and the
  helper answers DECODE with POOL: the session's control extent, where a picture ring and a
  release ring sit. Pictures arrive on the ring with the doorbell's interrupt; each names its slot
  by pool offset, and the buffer maps that slot's dma-buf (EXPORT). Freeing the buffer puts the
  slot on the release ring. Without the device, or when the pool is full, pictures come inline
  on the port. A `memory:DMABuf` caps feature is accepted, but the memory under it is ordinary
  mapped memory: the camera player only maps buffers (`gst_buffer_map`; no
  `gst_dmabuf_memory_get_fd`, no EGL import).

**`rvscale` (K7, K8).** W22H's camera player builds its PTZ pipeline
`... omxmjpegdec ! capsfilter NV12 ! videoconvert ! videocrop ! rvscale ! capsfilter
video/x-raw(memory:DMABuf),WxH ! appsink`. The stock `rvscale` opens the SoC scaler in its instance
init, and `se_open` asserts (se_drv.c:167) there, which aborted WebAppMgr's video-capture process.
The stand-in keeps the stock factory, rank (none), pad templates and metadata. At equal sizes it
passes the buffer through (the shared slot included). Otherwise it scales NV12 bilinearly in
software into system memory with a GstVideoMeta. Scaling on the host was not chosen: `videocrop`
sits between the decoder and `rvscale` and changes the crop at run time for PTZ, so the host would
have to take over the crop too. In the measured W22H runs the preview did not scale (640x480 in,
640x480 asked for). It is built where the firmware has `libgstrvscale.so`.

## Validation

The [TV feature suite](../guides/build.md#tv-feature-suite) exercises the actual
guest pipeline and host decoding. Static interface evidence, a synthetic element test and a
successful stock playback case establish different things; keep those distinctions in results.
`tests/regress/virtcam/run-camera.py NAME --app` checks the camera app's decode path end to end.
