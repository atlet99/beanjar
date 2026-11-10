// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// 48 kHz stereo input (Opus or PCM). Start with a small jitter margin, grow on
// starvation and recover slowly. Queue cutback is still bounded at 200 ms.
const RATE = 48000;
const MIN_TARGET = RATE * 0.04;
const MAX_TARGET = RATE * 0.16;
const BOUND = RATE * 0.2;
const RING = RATE;
const STEADY = RATE * 10; // ten seconds of playback between 5 ms reductions

class BeanjarSpeaker extends AudioWorkletProcessor {
  constructor() {
    super();
    this.l = new Float32Array(RING);
    this.r = new Float32Array(RING);
    this.head = 0;
    this.count = 0;
    this.target = MIN_TARGET;
    this.steady = 0;
    this.drain = 0;
    this.primed = false;
    this.stats = { received: 0, played: 0, underruns: 0, skips: 0, buffered: 0, target: this.target };
    this.reported = 0;
    this.port.onmessage = (e) => this.push(e.data.l, e.data.r);
  }

  push(l, r) {
    const n = l.length;
    this.stats.received += n;
    for (let i = 0; i < n; i++) {
      if (this.count === RING) { this.head = (this.head + 1) % RING; this.count--; }
      const at = (this.head + this.count) % RING;
      this.l[at] = l[i];
      this.r[at] = r[i];
      this.count++;
    }
    if (this.count > BOUND) {
      const drop = this.count - this.target;
      this.head = (this.head + drop) % RING;
      this.count -= drop;
      this.drain = 0;
      this.steady = 0;
      this.stats.skips++;
    }
  }

  process(_inputs, outputs) {
    const [outL, outR] = outputs[0];
    const n = outL.length;
    if (!this.primed && this.count >= this.target) this.primed = true;
    if (this.primed) {
      const take = Math.min(n, this.count);
      // Drain a reduced margin gently: consume one extra frame per quantum,
      // interpolated across the quantum, instead of a periodic audible 5 ms skip.
      const extra = this.drain > 0 && this.count > n ? 1 : 0;
      for (let i = 0; i < take; i++) {
        const pos = i + extra * i / n;
        const a = Math.floor(pos), f = pos - a;
        const at = (this.head + a) % RING, next = (at + 1) % RING;
        outL[i] = this.l[at] + f * (this.l[next] - this.l[at]);
        if (outR) outR[i] = this.r[at] + f * (this.r[next] - this.r[at]);
      }
      this.head = (this.head + take + extra) % RING;
      this.count -= take + extra;
      this.drain -= extra;
      this.stats.played += take;
      if (take < n) {
        this.stats.underruns++;
        this.primed = false;
        this.target = Math.min(MAX_TARGET, this.target + RATE * 0.02);
        this.steady = 0;
        this.drain = 0;
      } else {
        this.steady += n;
        if (this.steady >= STEADY) {
          this.steady = 0;
          const lower = Math.max(MIN_TARGET, this.target - RATE * 0.005);
          this.drain += this.target - lower;
          this.target = lower;
        }
      }
    }
    this.stats.buffered = this.count;
    this.stats.target = this.target;
    if (currentFrame - this.reported >= sampleRate / 2) {
      this.reported = currentFrame;
      this.port.postMessage(this.stats);
    }
    return true;
  }
}

registerProcessor("beanjar-speaker", BeanjarSpeaker);
