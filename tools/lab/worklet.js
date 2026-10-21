// Warp Lab audio thread: runs lab.wasm in 1024-sample blocks (the Blackbox DMA
// half) and serves them 128 frames at a time.
const BLOCK = 1024;

// Extra engines (Emscripten standalone): Rubber Band (rb.wasm, algos 10+) and
// Bungee (bungee.wasm, algos 20+). Exports are <prefix>_alloc/_in_*/_out_*/
// _render. WASI calls are never used by the stretchers, so every import is a no-op.
const EXT_BASE = { rb: 10, bg: 20 };
const extOf = (algo) => (algo >= 20 && algo < 30 ? 'bg' : algo >= 10 && algo < 20 ? 'rb' : null);

function instantiateExt(bytes) {
  const stub = new Proxy({}, { get: () => () => 0 });
  const inst = new WebAssembly.Instance(new WebAssembly.Module(bytes),
                                        { wasi_snapshot_preview1: stub, env: stub });
  const ex = inst.exports;
  if (ex._initialize) ex._initialize();
  return ex;
}

function loadExt(ex, p, l, r) {
  ex[`${p}_alloc`](l.length);
  new Float32Array(ex.memory.buffer, ex[`${p}_in_l`](), l.length).set(l);
  new Float32Array(ex.memory.buffer, ex[`${p}_in_r`](), r.length).set(r);
}

class LabProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.e = null;
    this.algo = 0;
    this.ratio = 1;
    this.gain = 0.8;
    this.playing = false;
    this.reset = true;
    this.loaded = false;
    this.fifoL = new Float32Array(BLOCK);
    this.fifoR = new Float32Array(BLOCK);
    this.have = 0;
    this.pos = 0;
    this.peak = 0;
    this.port.onmessage = (ev) => this.onMessage(ev.data);
  }

  async onMessage(m) {
    if (m.type === 'init') {
      try {
        // Raw bytes, compiled here: a compiled Module does not reach every
        // browser's worklet scope.
        const inst = new WebAssembly.Instance(new WebAssembly.Module(m.bytes), {});
        this.e = inst.exports;
        this.ext = {};
        for (const p in m.extBytes || {}) if (m.extBytes[p]) this.ext[p] = instantiateExt(m.extBytes[p]);
        this.port.postMessage({ type: 'ready' });
      } catch (err) {
        this.port.postMessage({ type: 'error', message: String(err) });
      }
    } else if (m.type === 'load' && this.e) {
      const mem = this.e.memory.buffer;
      const n = Math.min(m.l.length, this.e.lab_max_frames());
      new Float32Array(mem, this.e.lab_in_l(), n).set(m.l.subarray(0, n));
      new Float32Array(mem, this.e.lab_in_r(), n).set((m.r || m.l).subarray(0, n));
      const marks = this.e.lab_load(n, m.r ? 1 : 0);
      for (const p in this.ext) loadExt(this.ext[p], p, m.l.subarray(0, n), (m.r || m.l).subarray(0, n));
      this.loaded = true;
      this.restart();
      this.port.postMessage({ type: 'loaded', frames: n, marks });
    } else if (m.type === 'params') {
      if (m.algo !== undefined && m.algo !== this.algo) { this.algo = m.algo; this.restart(); }
      if (m.ratio !== undefined) this.ratio = m.ratio;
      if (m.gain !== undefined) this.gain = m.gain;
      if (m.playing !== undefined) { if (m.playing && !this.playing) this.restart(); this.playing = m.playing; }
      if (m.gran && this.e) {
        const g = m.gran;
        this.e.lab_gran_params(g.size, g.dens, g.scat, g.win, g.speed, g.pan, g.root);
        if (this.algo === 30 || this.algo === 31) this.restart();
      }
      if (m.capi && this.e) {
        const c = m.capi;
        this.e.lab_capi_params(c.leash, c.fade, c.eps, c.thresh, c.onset ? 1 : 0);
        if (this.algo === 3) this.restart();
      }
      if (m.sync && this.e) {
        this.e.lab_sync_params(m.sync.bpm, m.sync.perBeat);
        if (this.loaded) this.postSlices();
        if (this.algo >= 6) this.restart();
      }
      if (m.beats && this.e) {
        const b = m.beats;
        this.e.lab_beats_params(b.fill, b.attack, b.cut, b.xf, b.pp, b.glen, b.dens);
        this.sens = b.sens;
        this.gap = b.gap;
        if (this.loaded) this.postSlices();
        if (this.algo === 7 || this.algo === 8) this.restart();
      }
      if (m.restart) this.restart();
    }
  }

  postSlices() {
    const count = this.e.lab_slices(this.sens || 4);
    const total = this.e.lab_slice_gap(this.gap === undefined ? 2.5 : this.gap);
    this.port.postMessage({ type: 'slices', count, total });
  }

  restart() {
    this.reset = true;
    this.have = 0;
    this.pos = 0;
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const L = out[0], R = out[1] || out[0];
    if (!this.e || !this.loaded || !this.playing) { L.fill(0); R.fill(0); return true; }
    let i = 0;
    while (i < L.length) {
      if (this.pos >= this.have) {
        const p = extOf(this.algo);
        const ex = p ? this.ext[p] : this.e;
        if (!ex) { L.fill(0, i); R.fill(0, i); return true; }
        const pre = p || 'lab';
        ex[`${pre}_render`](p ? this.algo - EXT_BASE[p] : this.algo, BLOCK, this.ratio, this.reset ? 1 : 0);
        this.reset = false;
        const mem = ex.memory.buffer;
        this.fifoL.set(new Float32Array(mem, ex[`${pre}_out_l`](), BLOCK));
        this.fifoR.set(new Float32Array(mem, ex[`${pre}_out_r`](), BLOCK));
        this.have = BLOCK;
        this.pos = 0;
      }
      const take = Math.min(L.length - i, this.have - this.pos);
      for (let k = 0; k < take; k++) {
        const l = this.fifoL[this.pos + k] * this.gain, r = this.fifoR[this.pos + k] * this.gain;
        const a = Math.max(Math.abs(l), Math.abs(r));
        if (a > this.peak) this.peak = a;
        L[i + k] = Math.max(-1, Math.min(1, l));
        R[i + k] = Math.max(-1, Math.min(1, r));
      }
      this.pos += take;
      i += take;
    }
    if ((currentFrame & 8191) < 128) { this.port.postMessage({ type: 'meter', peak: this.peak }); this.peak *= 0.5; }
    return true;
  }
}

registerProcessor('lab', LabProcessor);
