// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The speaker: the VM's mixed audio over /vm/HOST/NAME/audio (beanjar vnc audio-pipe's stream,
// host/vnc/audio.h), played through an AudioWorklet jitter buffer (audio-worklet.js). Opus
// decoded with WebCodecs' AudioDecoder where the page has it (a secure context: https, or
// http://127.0.0.1 and localhost), else raw 48 kHz s16 PCM. docs/dev/architecture/web-viewer.md.
const RATE = 48000;
const FORMATS = { 1: "pcm", 2: "opus" };

// "opus" when this browser decodes it here, else "pcm"
export async function bestFormat() {
  if (typeof AudioDecoder === "undefined") return "pcm";
  try {
    const r = await AudioDecoder.isConfigSupported({ codec: "opus", sampleRate: RATE, numberOfChannels: 2 });
    return r.supported ? "opus" : "pcm";
  } catch {
    return "pcm";
  }
}

// RFC 6716 section 3.1: TOC configuration and frame-count code. Older servers send
// 20 ms packets, so derive local WebCodecs timestamps instead of assuming 10 ms.
function opusDuration(data) {
  const config = data[0] >> 3;
  const frame = config >= 16 ? 2500 * (1 << (config & 3))
    : config >= 12 ? 10000 * (1 << (config & 1))
    : (config & 3) === 3 ? 60000 : 10000 * (1 << (config & 3));
  const code = data[0] & 3;
  return frame * (code === 0 ? 1 : code === 3 ? data[1] & 63 : 2);
}

export class Speaker {
  // url: the audio WebSocket without its query; onstate(text, playing): for the button
  constructor(url, onstate) {
    this.url = url;
    this.onstate = onstate;
    this.ctx = null;
    this.ws = null;
    this.decoder = null;
    this.format = null;
    this.retry = null;
    // what the page reads (and the tests): the worklet's counters, packets and decoded frames
    this.stats = { format: null, messages: 0, bytes: 0, packets: 0, decoded: 0, worklet: {} };
  }

  // From a click (browsers start audio only from a user gesture).
  async start() {
    if (this.ctx) return;
    let ctx;
    try {
      ctx = new AudioContext({ sampleRate: RATE, latencyHint: 0 });
      this.stats.latencyHint = 0;
    } catch {
      // Some engines reject a numeric hint. Keep the existing interactive path.
      ctx = new AudioContext({ sampleRate: RATE, latencyHint: "interactive" });
      this.stats.latencyHint = "interactive";
    }
    this.ctx = ctx;
    await ctx.audioWorklet.addModule("/audio-worklet.js");
    if (this.ctx !== ctx) return; // stopped meanwhile
    this.node = new AudioWorkletNode(this.ctx, "beanjar-speaker", { outputChannelCount: [2] });
    this.node.port.onmessage = (e) => { this.stats.worklet = e.data; };
    this.node.connect(this.ctx.destination);
    if (this.ctx.state !== "running") await this.ctx.resume().catch(() => {});
    this.format = await bestFormat();
    if (this.ctx !== ctx) return;
    this.stats.format = this.format;
    this.connect();
  }

  stop() {
    clearTimeout(this.retry);
    this.retry = null;
    if (this.ws) { this.ws.onclose = null; this.ws.close(); }
    this.ws = null;
    if (this.node) this.node.disconnect();
    if (this.decoder && this.decoder.state !== "closed") this.decoder.close();
    this.decoder = null;
    if (this.ctx) this.ctx.close();
    this.ctx = null;
  }

  connect() {
    if (!this.ctx) return;
    if (this.decoder && this.decoder.state !== "closed") this.decoder.close();
    this.decoder = null;
    const ws = new WebSocket(`${this.url}?format=${this.format}`);
    ws.binaryType = "arraybuffer";
    this.ws = ws;
    this.buf = new Uint8Array(0);
    this.header = false;
    this.timestamp = 0;
    ws.onopen = () => { if (this.ws === ws) this.onstate(this.format, true); };
    ws.onmessage = (e) => { if (this.ws === ws) this.receive(new Uint8Array(e.data)); };
    ws.onclose = (e) => {
      if (this.ws !== ws) return;
      this.ws = null;
      this.onstate(e.reason || "the audio stream ended", false);
      // the VM restarted beanjar-vnc, or the network dropped: again, while still on
      this.retry = setTimeout(() => { if (this.ctx && !this.ws) this.connect(); }, 3000);
    };
  }

  receive(data) {
    this.stats.messages++;
    this.stats.bytes += data.length;
    const buf = new Uint8Array(this.buf.length + data.length);
    buf.set(this.buf);
    buf.set(data, this.buf.length);
    let at = 0;
    if (!this.header) {
      if (buf.length < 16) { this.buf = buf; return; }
      const v = new DataView(buf.buffer, buf.byteOffset, 16);
      const magic = String.fromCharCode(...buf.subarray(0, 4));
      const fmt = FORMATS[v.getUint16(14, true)];
      if (magic !== "BJAU" || v.getUint32(8, true) !== RATE || v.getUint16(12, true) !== 2 || fmt !== this.format) {
        this.ws.close(4001, "not the audio stream asked for");
        return;
      }
      this.header = true;
      at = v.getUint32(4, true);
      if (fmt === "opus") this.openDecoder();
    }
    at = this.format === "opus" ? this.packets(buf, at) : this.pcm(buf, at);
    this.buf = buf.slice(at);
  }

  pcm(buf, at) {
    const frames = Math.floor((buf.length - at) / 4);
    if (!frames) return at;
    const v = new DataView(buf.buffer, buf.byteOffset + at, frames * 4);
    const l = new Float32Array(frames), r = new Float32Array(frames);
    for (let i = 0; i < frames; i++) {
      l[i] = v.getInt16(4 * i, true) / 32768;
      r[i] = v.getInt16(4 * i + 2, true) / 32768;
    }
    this.node.port.postMessage({ l, r }, [l.buffer, r.buffer]);
    return at + frames * 4;
  }

  openDecoder() {
    this.decoder = new AudioDecoder({
      output: (d) => {
        const l = new Float32Array(d.numberOfFrames), r = new Float32Array(d.numberOfFrames);
        d.copyTo(l, { planeIndex: 0, format: "f32-planar" });
        d.copyTo(r, { planeIndex: d.numberOfChannels > 1 ? 1 : 0, format: "f32-planar" });
        d.close();
        this.stats.decoded += l.length;
        if (this.node) this.node.port.postMessage({ l, r }, [l.buffer, r.buffer]);
      },
      error: (e) => console.warn(`opus: ${e.message}`),
    });
    this.decoder.configure({ codec: "opus", sampleRate: RATE, numberOfChannels: 2 });
  }

  packets(buf, at) {
    while (buf.length - at >= 2) {
      const n = buf[at] | (buf[at + 1] << 8);
      if (buf.length - at < 2 + n) break;
      const data = buf.slice(at + 2, at + 2 + n);
      at += 2 + n;
      this.stats.packets++;
      if (this.decoder && this.decoder.state === "configured") {
        this.decoder.decode(new EncodedAudioChunk({ type: "key", timestamp: this.timestamp, data }));
      }
      this.timestamp += opusDuration(data); // us, from the packet TOC (also accepts older servers)
    }
    return at;
  }
}
