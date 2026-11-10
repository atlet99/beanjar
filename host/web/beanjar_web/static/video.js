// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The H.264 picture: the TV picture over /vm/HOST/NAME/video (beanjar vnc video-pipe's stream,
// host/vnc/video.h), decoded with WebCodecs' VideoDecoder, drawn on a canvas laid over noVNC's.
// WebCodecs needs a secure context (https, or http://127.0.0.1 and localhost); without one the
// viewer stays on VNC (Tight). docs/dev/architecture/web-viewer.md.
const HEADER = 32, UNIT_HEAD = 16, CODEC_H264 = 1;
// what the encoders make: x264's Constrained Baseline, NVENC's and VA-API's Main (level 4.0 covers
// 1080p30; the stream's own SPS gives the codec string it is decoded with)
const PROBES = ["avc1.42C028", "avc1.4D4028"];

// null when this browser can decode the stream here, else why not
export async function h264Unsupported() {
  if (typeof VideoDecoder === "undefined") {
    return window.isSecureContext ? "this browser has no WebCodecs VideoDecoder"
      : "WebCodecs needs a secure context: https, or http://127.0.0.1 or localhost";
  }
  for (const codec of PROBES) {
    try {
      const r = await VideoDecoder.isConfigSupported({ codec, optimizeForLatency: true });
      if (r.supported) return null;
    } catch { /* the next */ }
  }
  return "this browser decodes no H.264 (avc1)";
}

// "avc1.PPCCLL" from the SPS in an Annex-B access unit, or null
function codecOf(au) {
  for (let i = 0; i + 7 <= au.length; i++) {
    if (au[i] === 0 && au[i + 1] === 0 && au[i + 2] === 1 && (au[i + 3] & 0x1f) === 7) {
      return "avc1." + [au[i + 4], au[i + 5], au[i + 6]].map((b) => b.toString(16).padStart(2, "0")).join("").toUpperCase();
    }
  }
  return null;
}

export class Picture {
  // url: the video WebSocket with its query; canvas: the overlay; onstate(state, detail):
  // "playing" (the encoder's name), "failed" (why)
  constructor(url, canvas, onstate) {
    this.url = url;
    this.canvas = canvas;
    this.ctx2d = canvas.getContext("2d");
    this.onstate = onstate;
    this.ws = null;
    this.decoder = null;
    this.stopped = false;
    // what Stats for nerds shows (and the tests read): totals, and the last second's rates
    this.stats = { encoder: null, width: 0, height: 0, codec: null, units: 0, keyframes: 0,
      bytes: 0, decoded: 0, drawn: 0, fps: 0, kbps: 0, hardware: null };
    this.window = { t: performance.now(), drawn: 0, bytes: 0 };
  }

  start() {
    const ws = new WebSocket(this.url);
    ws.binaryType = "arraybuffer";
    this.ws = ws;
    this.buf = new Uint8Array(0);
    this.header = false;
    ws.onmessage = (e) => { if (!this.stopped && this.ws === ws) this.receive(new Uint8Array(e.data)); };
    ws.onclose = (e) => {
      this.ws = null;
      if (!this.stopped) this.fail(e.reason || "the video stream ended");
    };
    this.timer = setInterval(() => this.tick(), 1000);
  }

  stop() {
    this.stopped = true;
    clearInterval(this.timer);
    if (this.ws) { this.ws.onclose = null; this.ws.close(); }
    this.ws = null;
    if (this.decoder && this.decoder.state !== "closed") this.decoder.close();
    this.decoder = null;
  }

  fail(why) {
    if (this.stopped) return;
    this.stop();
    this.onstate("failed", why);
  }

  tick() {
    const now = performance.now(), w = this.window, s = this.stats, dt = (now - w.t) / 1000;
    s.fps = (s.drawn - w.drawn) / dt;
    s.kbps = (s.bytes - w.bytes) * 8 / dt / 1000;
    this.window = { t: now, drawn: s.drawn, bytes: s.bytes };
  }

  receive(data) {
    let buf = data;
    if (this.buf.length) {
      buf = new Uint8Array(this.buf.length + data.length);
      buf.set(this.buf);
      buf.set(data, this.buf.length);
    }
    let at = 0;
    if (!this.header) {
      if (buf.length < HEADER) { this.buf = buf; return; }
      const v = new DataView(buf.buffer, buf.byteOffset, HEADER);
      const magic = String.fromCharCode(...buf.subarray(0, 4));
      if (magic !== "BJVI" || v.getUint16(14, true) !== CODEC_H264) return this.fail("not an H.264 stream");
      this.header = true;
      at = v.getUint32(4, true);
      Object.assign(this.stats, {
        width: v.getUint16(8, true), height: v.getUint16(10, true),
        encoder: new TextDecoder().decode(buf.subarray(20, HEADER)).replace(/\0.*$/s, ""),
      });
      this.canvas.width = this.stats.width;
      this.canvas.height = this.stats.height;
    }
    while (buf.length - at >= UNIT_HEAD) {
      const v = new DataView(buf.buffer, buf.byteOffset + at, UNIT_HEAD);
      const n = v.getUint32(0, true), key = (v.getUint32(4, true) & 1) === 1;
      if (buf.length - at < UNIT_HEAD + n) break;
      const ts = v.getUint32(8, true) + v.getUint32(12, true) * 2 ** 32;
      this.unit(buf.subarray(at + UNIT_HEAD, at + UNIT_HEAD + n), key, ts);
      at += UNIT_HEAD + n;
      if (this.stopped) return;
    }
    this.buf = buf.slice(at);
  }

  unit(au, key, timestamp) {
    const s = this.stats;
    s.units++;
    s.bytes += au.length + UNIT_HEAD;
    if (key) s.keyframes++;
    if (!this.decoder) {
      if (!key) return; // the stream starts with one; nothing to decode before it
      this.open(au);
      if (!this.decoder) return;
    }
    try {
      this.decoder.decode(new EncodedVideoChunk({ type: key ? "key" : "delta", timestamp, data: au }));
    } catch (e) {
      this.fail(`decoding: ${e.message}`);
    }
  }

  open(au) {
    const codec = codecOf(au);
    if (!codec) return this.fail("a keyframe without its SPS");
    this.stats.codec = codec;
    this.decoder = new VideoDecoder({
      output: (frame) => this.draw(frame),
      error: (e) => this.fail(`the decoder: ${e.message}`),
    });
    const config = { codec, optimizeForLatency: true, codedWidth: this.stats.width, codedHeight: this.stats.height };
    VideoDecoder.isConfigSupported(config).then((r) => {
      if (!r.supported) this.fail(`this browser does not decode ${codec}`);
    }).catch(() => {});
    // whether this browser has a hardware decoder for it (WebCodecs does not say which it uses)
    VideoDecoder.isConfigSupported({ ...config, hardwareAcceleration: "prefer-hardware" })
      .then((r) => { this.stats.hardware = r.supported; }).catch(() => {});
    try {
      this.decoder.configure(config);
    } catch (e) {
      return this.fail(`${codec}: ${e.message}`);
    }
    this.onstate("playing", this.stats.encoder);
  }

  draw(frame) {
    this.stats.decoded++;
    if (!this.stopped) {
      this.ctx2d.drawImage(frame, 0, 0, this.canvas.width, this.canvas.height);
      this.stats.drawn++;
    }
    frame.close();
  }
}
