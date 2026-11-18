/**
 * PRISM's engine on the SHARC-only rig: the assembled sources behind the production effect adapter (core/fx), with stand-ins
 * for the storage descriptor and the two mailboxes (provider.s). engine.rig.test.ts and render-audio.ts run it.
 *
 *     const rig = await PrismRig.open({ frames: 16384 });
 *     rig.set({ zone: 4, rate: 0.5, rpts: 0.5 });
 *     const take = await rig.run(600, sine(220, 0.25));   // 600 blocks: take.left, take.right (the wet signal), take.instructions
 *
 * Every block is held to the rules of the effect contract: the hook returns, the context and the registers come back, the
 * adapter counts no fault and the output is finite. A case that expects a fault opens the rig with `faults: true`.
 */
import path from 'node:path';
import { FxRig, SW_ALIAS_BASE, f2w, fft, w2f } from '../../../../packages/sharc-rig-kit/src/index.ts';

export const FS = 48000;
export const BLOCK = 32;
/** The stand-in bank (external memory, as the device leases it). */
export const BANK = 0x9aa2b9c8;
/** A bank as the device leases it: 21.5 s. */
export const DEVICE_FRAMES = 1032192;
const HERE = import.meta.dirname;
const DSP = path.join(HERE, '../src/dsp');
const CONTROL_MAGIC = 0x50484331;
const CONTROL_VERSION = 3;
const POISON = 0x7fc00001;

/** State words of prism.h that the tests read. */
export const PS = {
  WPOS: 10,
  VALID: 11,
  HOLD: 12,
  POST: 13,
  HOLD_W: 14,
  WFADE: 15,
  XFADE: 16,
  TIME_L: 34,
  TIME_R: 35,
  CH_L: 128,
  CH_R: 160,
} as const;
/** Words of one side's record (prism.h CH_*). */
export const CH = { A_KIND: 0, A_DINT: 1, A_FRAC: 2, A_DIR: 3, B_KIND: 4, VEL: 8, TARGET: 9, FADE_LEFT: 12 } as const;

export interface Controls {
  /** 1..8 */
  zone: number;
  /** 0..1, the position inside the zone */
  rate: number;
  sync: boolean;
  flip: boolean;
  hold: boolean;
  /** with `sync`: the time in frames */
  syncTime: number;
  /** 0..1: 0.75 is unity */
  rpts: number;
  /** -1 dark .. 1 bright */
  colr: number;
  halo: number;
  /** -1..1: above 0 the left side is the shorter one */
  skew: number;
  mix: number;
}

/** PRISM_XFADE and PRISM_WFADE of prism.h. */
export const XFADE = 960;
export const WFADE = 480;

export const DEFAULTS: Controls = {
  zone: 4,
  rate: 0.5,
  sync: false,
  flip: false,
  hold: false,
  syncTime: 12000,
  rpts: 0.5,
  colr: 0,
  halo: 0,
  skew: 0,
  mix: 1,
};

/** The zones' free ranges in frames (prism.s `_prism_zones`). */
export const ZONES: readonly (readonly [number, number])[] = [
  [48, 480],
  [240, 1920],
  [960, 5760],
  [2880, 16800],
  [7200, 48000],
  [24000, 129600],
  [62400, 259200],
  [129600, 513600],
];

/** The time in frames the engine's law gives for a zone and a rate: low * (high / low) ^ rate. */
export function zoneTime(zone: number, rate: number): number {
  const [low, high] = ZONES[zone - 1]!;
  return low * (high / low) ** rate;
}

/** A signal: the two sides' samples at a frame, or one value for both. */
export type Signal = (frame: number) => number | readonly [number, number];

export const silence: Signal = () => 0;
export const sine =
  (hz: number, amp: number): Signal =>
  (n) =>
    amp * Math.sin((2 * Math.PI * hz * n) / FS);
/** A tone that sounds from frame `from` for `frames`, with a raised-cosine edge of `edge` frames at both ends. */
export function burst(hz: number, amp: number, from: number, frames: number, edge = 96): Signal {
  return (n) => {
    const p = n - from;
    if (p < 0 || p >= frames) return 0;
    const e = Math.min(1, p / edge, (frames - p) / edge);
    return amp * 0.5 * (1 - Math.cos(Math.PI * e)) * Math.sin((2 * Math.PI * hz * p) / FS);
  };
}
export const sum =
  (...signals: Signal[]): Signal =>
  (n) => {
    let l = 0;
    let r = 0;
    for (const s of signals) {
      const v = s(n);
      if (typeof v === 'number') {
        l += v;
        r += v;
      } else {
        l += v[0];
        r += v[1];
      }
    }
    return [l, r];
  };

export interface Take {
  /** the frame of the rig's clock at the take's first sample */
  start: number;
  /** the wet signal (the effect's output block) */
  left: Float32Array;
  right: Float32Array;
  /** the input, as given */
  dryLeft: Float32Array;
  dryRight: Float32Array;
  /** instructions of each hook call (core/fx and the effect) */
  instructions: number[];
  /** blocks in which the adapter counted a fault (only with `faults: true`) */
  faulted: number;
}

export interface PrismRigOptions {
  /** frames of the bank (a multiple of 32, at least 4096); default 16384 */
  frames?: number;
  /** fill the bank with NaN words first, so a read of tape that was never written shows (default for banks up to 65536 frames) */
  poison?: boolean;
  /** the engine without HALO: tests/halo-off.s in place of src/dsp/halo.s */
  haloOff?: boolean;
  /** record the stores of the module's code (slower) */
  stores?: boolean;
  /** faults are expected: count them instead of throwing */
  faults?: boolean;
  /** a friendly register context instead of a hostile one for every block */
  friendly?: boolean;
}

export class PrismRig {
  /** The heaviest hook call (core/fx and the effect) of every rig of this process, and the block it was. */
  static worst = 0;
  static worstIn = '';
  readonly fx: FxRig;
  readonly frames: number;
  readonly controls: Controls = { ...DEFAULTS };
  /** frames run so far: the argument of the next input sample */
  clock = 0;
  /** stores of the module's code outside its own memory and the bank, as text (with `stores`) */
  readonly foreign: string[] = [];
  /** stores into the bank in the last `run` (with `stores`) */
  bankStores = 0;
  private sequence = 0;
  private faultCount = 0;
  private readonly o: PrismRigOptions;

  private constructor(fx: FxRig, frames: number, o: PrismRigOptions) {
    this.fx = fx;
    this.frames = frames;
    this.o = o;
  }

  static async open(o: PrismRigOptions = {}): Promise<PrismRig> {
    const frames = o.frames ?? 16384;
    const sources = ['prism.s', 'heads.s', 'loop.s', o.haloOff ? '' : 'halo.s']
      .filter(Boolean)
      .map((name) => path.join(DSP, name));
    if (o.haloOff) sources.push(path.join(HERE, 'halo-off.s'));
    sources.push(path.join(HERE, 'provider.s'));
    const fx = await FxRig.open([{ pc: '_prism_process', state: '', desc: '_prism_desc' }], {
      sources,
      stepLimit: 400_000,
      recordStores: o.stores ?? false,
    });
    const rig = new PrismRig(fx, frames, o);
    const info = fx.built.data._phrase_storage_info!;
    fx.poke(info + 16, 8 * frames);
    fx.poke(info + 20, frames);
    if (o.poison ?? frames <= 65536) for (let k = 0; k < 2 * frames; k++) fx.poke(BANK + 4 * k, POISON);
    rig.set({});
    return rig;
  }

  close(): Promise<void> {
    return this.fx.close();
  }

  /** Publish controls: the words of prism.h in the control mailbox, with a new sequence. */
  set(change: Partial<Controls>): void {
    Object.assign(this.controls, change);
    const c = this.controls;
    const at = this.fx.built.data._phrase_control_info!;
    this.sequence += 1;
    const words: Record<number, number> = {
      0: CONTROL_MAGIC,
      1: CONTROL_VERSION,
      2: 256,
      3: this.sequence,
      4: 1,
      8: c.zone,
      9: f2w(c.rate),
      10: (c.sync ? 1 : 0) | (c.flip ? 2 : 0) | (c.hold ? 4 : 0),
      11: c.syncTime,
      12: f2w(c.rpts),
      13: f2w(c.colr),
      14: f2w(c.halo),
      15: f2w(c.skew),
      16: f2w(c.mix),
      56: 7,
      62: this.sequence,
    };
    for (const [word, value] of Object.entries(words)) this.fx.poke(at + 4 * Number(word), value);
  }

  /** One raw word of the control mailbox or the storage descriptor (a case that breaks the link). */
  pokeControl(word: number, value: number): void {
    this.fx.poke(this.fx.built.data._phrase_control_info! + 4 * word, value);
  }

  pokeStorage(word: number, value: number): void {
    this.fx.poke(this.fx.built.data._phrase_storage_info! + 4 * word, value);
  }

  /** Write a word of the engine's state (a constant a test tunes, such as the crossfade length). */
  pokeState(word: number, value: number): void {
    this.fx.poke(this.fx.stateAddress(0) + 4 * word, value);
  }

  async state(word: number, count = 1): Promise<number[]> {
    const words = await this.fx.peek(this.fx.stateAddress(0) + 4 * word, count);
    return words.map((w) => w ?? 0);
  }

  async status(count = 32): Promise<number[]> {
    const words = await this.fx.peek(this.fx.built.data._phrase_status!, count);
    return words.map((w) => w ?? 0);
  }

  /** The tape as it stands: the two planes' words (null: never written). */
  async tape(): Promise<(number | null)[]> {
    return this.fx.peek(BANK, 2 * this.frames);
  }

  /** Run `blocks` blocks with `input` (a function of the rig's clock). */
  async run(blocks: number, input: Signal = silence): Promise<Take> {
    const n = blocks * BLOCK;
    const take: Take = {
      start: this.clock,
      left: new Float32Array(n),
      right: new Float32Array(n),
      dryLeft: new Float32Array(n),
      dryRight: new Float32Array(n),
      instructions: [],
      faulted: 0,
    };
    const master = new Array<number>(2 * BLOCK);
    this.bankStores = 0;
    for (let b = 0; b < blocks; b++) {
      for (let k = 0; k < BLOCK; k++) {
        const v = input(this.clock + k);
        const [l, r] = typeof v === 'number' ? [v, v] : v;
        master[k] = l;
        master[BLOCK + k] = r;
        take.dryLeft[b * BLOCK + k] = l;
        take.dryRight[b * BLOCK + k] = r;
      }
      const block = await this.fx.block(master, this.o.friendly ? 'friendly' : undefined);
      const where = `block ${this.clock / BLOCK}`;
      if (!block.returned) throw new Error(`${where}: the hook did not return (${block.halt} at pc 0x${block.finalPc.toString(16)})`);
      if (block.ctxBad.length || block.regsBad.length)
        throw new Error(`${where}: registers came back changed: ${[...block.ctxBad, ...block.regsBad].join(' ')}`);
      // the adapter's fault count of the row: a block that raised it played dry
      const faulted = block.faults[0]! !== this.faultCount;
      this.faultCount = block.faults[0]!;
      if (faulted && !this.o.faults) throw new Error(`${where}: the adapter counted a fault`);
      if (faulted) {
        take.faulted += 1;
        if (master.some((v, k) => f2w(block.master[k]!) !== f2w(v))) throw new Error(`${where}: a faulted block did not play dry`);
      }
      for (let k = 0; k < BLOCK; k++) {
        const l = faulted ? 0 : block.scratch[k]!;
        const r = faulted ? 0 : block.scratch[BLOCK + k]!;
        if (!Number.isFinite(l) || !Number.isFinite(r)) throw new Error(`${where}: the output is not finite`);
        take.left[b * BLOCK + k] = l;
        take.right[b * BLOCK + k] = r;
      }
      take.instructions.push(block.instructions);
      if (block.instructions > PrismRig.worst) {
        PrismRig.worst = block.instructions;
        PrismRig.worstIn = `${where}, ${JSON.stringify(this.controls)}`;
      }
      if (this.o.stores) this.checkStores(block.stores, where);
      this.clock += BLOCK;
    }
    return take;
  }

  /** The module's code may store to its output block, its state and data, and its bank; never to the mailbox it reads or the descriptor. */
  private checkStores(stores: [pc: number, address: number, width: number][], where: string): void {
    const built = this.fx.built;
    const readOnly = [
      { address: built.data._phrase_control_info!, bytes: 256 },
      { address: built.data._phrase_storage_info!, bytes: 24 },
    ];
    // the state lies in the rig's auto-state area: a store there stays inside the descriptor's state_bytes
    const state = { address: this.fx.stateAddress(0), bytes: this.fx.descWords('_prism_desc')[3]! };
    const autoStates = { address: state.address, bytes: 4 * 4096 };
    const own = [{ address: built.data._fx_scratch!, bytes: 256 }, ...built.moduleData];
    for (const [pc, at, width] of stores) {
      if (!built.moduleCode.some(([lo, hi]) => lo <= pc && pc < hi)) continue;
      // the core keeps internal memory at the loader alias
      const address = SW_ALIAS_BASE <= at && at < 0x80000000 ? at - SW_ALIAS_BASE : at;
      if (BANK <= address && address + width <= BANK + 8 * this.frames) {
        this.bankStores += 1;
        continue;
      }
      const inside = (r: { address: number; bytes: number }) => r.address <= address && address + width <= r.address + r.bytes;
      if (inside(state) || (own.some(inside) && !readOnly.some(inside) && !inside(autoStates))) continue;
      if (this.foreign.length < 8) this.foreign.push(`${where}: store to 0x${address.toString(16)} from pc 0x${pc.toString(16)}`);
    }
  }
}

// ---- measures -------------------------------------------------------------------------------------------------------------------

/** The largest second difference of x in [from, to): what a click or a kink shows as. */
export function maxSecondDifference(x: ArrayLike<number>, from = 0, to = x.length): { value: number; at: number } {
  let value = 0;
  let at = from;
  for (let n = Math.max(from, 1); n < Math.min(to, x.length) - 1; n++) {
    const d = Math.abs(x[n + 1]! - 2 * x[n]! + x[n - 1]!);
    if (d > value) {
      value = d;
      at = n;
    }
  }
  return { value, at };
}

/** The largest second difference of a clean sine of this peak and frequency. */
export const cleanSecondDifference = (peak: number, hz: number): number => peak * (2 * Math.sin((Math.PI * hz) / FS)) ** 2;

export function rms(x: ArrayLike<number>, from = 0, to = x.length): number {
  let s = 0;
  for (let n = from; n < to; n++) s += x[n]! * x[n]!;
  return Math.sqrt(s / Math.max(1, to - from));
}

export function peak(x: ArrayLike<number>, from = 0, to = x.length): number {
  let m = 0;
  for (let n = from; n < to; n++) m = Math.max(m, Math.abs(x[n]!));
  return m;
}

/** The amplitude of the component at `hz` in [from, to) (a Hann-windowed projection). */
export function levelAt(x: ArrayLike<number>, hz: number, from = 0, to = x.length): number {
  let re = 0;
  let im = 0;
  let weight = 0;
  const n = to - from;
  for (let k = 0; k < n; k++) {
    const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * (k + 0.5)) / n);
    const p = (2 * Math.PI * hz * k) / FS;
    re += w * x[from + k]! * Math.cos(p);
    im += w * x[from + k]! * Math.sin(p);
    weight += w;
  }
  return (2 * Math.hypot(re, im)) / weight;
}

/** The frequency in [from, to) from the upward zero crossings (interpolated), 0 with fewer than two. */
export function crossingHz(x: ArrayLike<number>, from = 0, to = x.length): number {
  let first = -1;
  let last = -1;
  let count = 0;
  for (let n = from + 1; n < to; n++) {
    if (x[n - 1]! < 0 && x[n]! >= 0) {
      const t = n - 1 + x[n - 1]! / (x[n - 1]! - x[n]!);
      if (first < 0) first = t;
      else count += 1;
      last = t;
    }
  }
  return count ? (count * FS) / (last - first) : 0;
}

/** The lag (0 .. maxLag) at which y best matches x (the largest normalised cross-correlation). */
export function bestLag(x: ArrayLike<number>, y: ArrayLike<number>, maxLag: number, from = 0, length = x.length - from): number {
  let best = 0;
  let bestScore = -Infinity;
  for (let lag = 0; lag <= maxLag; lag++) {
    let s = 0;
    for (let n = 0; n < length && from + n + lag < y.length; n++) s += x[from + n]! * y[from + n + lag]!;
    if (s > bestScore) {
      bestScore = s;
      best = lag;
    }
  }
  return best;
}

export const db = (ratio: number): number => 20 * Math.log10(ratio);


/** The rms of the band [lo, hi] Hz in [from, to) (Hann window, power spectrum). */
export function bandLevel(x: ArrayLike<number>, lo: number, hi: number, from = 0, to = x.length): number {
  let size = 1;
  while (size < to - from) size *= 2;
  const n = to - from;
  const re = new Array<number>(size).fill(0);
  let weight = 0;
  for (let k = 0; k < n; k++) {
    const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * (k + 0.5)) / n);
    re[k] = w * x[from + k]!;
    weight += w * w;
  }
  const [r, i] = fft(re, new Array<number>(size).fill(0));
  let power = 0;
  for (let k = Math.ceil((lo * size) / FS); k <= Math.floor((hi * size) / FS); k++) power += r[k]! ** 2 + i[k]! ** 2;
  return Math.sqrt((2 * power) / (size * weight));
}

/** White noise from a seeded generator, the same on both sides; it has to be asked for in rising frame order. */
export function noise(amp: number, seed = 1): Signal {
  const recent = new Map<number, number>();
  let s = seed >>> 0;
  let next = 0;
  return (n) => {
    while (next <= n) {
      s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
      recent.set(next++, amp * (s / 2 ** 31 - 1));
      recent.delete(next - 4097);
    }
    return recent.get(n) ?? 0;
  };
}

/** The rms of windows of `win` samples (half overlapped) in [from, to): the lowest and the highest against the median. */
export function envelope(
  x: ArrayLike<number>,
  from: number,
  to: number,
  win = 240,
): { low: number; high: number; median: number } {
  const levels: number[] = [];
  for (let a = from; a + win <= to; a += win / 2) levels.push(rms(x, a, a + win));
  const sorted = [...levels].sort((p, q) => p - q);
  const median = sorted[sorted.length >> 1]!;
  return { low: sorted[0]! / median, high: sorted.at(-1)! / median, median };
}

/** Takes joined end to end. */
export function join(takes: Take[]): Take {
  const n = takes.reduce((total, t) => total + t.left.length, 0);
  const out: Take = {
    start: takes[0]!.start,
    left: new Float32Array(n),
    right: new Float32Array(n),
    dryLeft: new Float32Array(n),
    dryRight: new Float32Array(n),
    instructions: [],
    faulted: 0,
  };
  let at = 0;
  for (const t of takes) {
    out.left.set(t.left, at);
    out.right.set(t.right, at);
    out.dryLeft.set(t.dryLeft, at);
    out.dryRight.set(t.dryRight, at);
    out.instructions.push(...t.instructions);
    out.faulted += t.faulted;
    at += t.left.length;
  }
  return out;
}

/** Run steps of [blocks, controls to change first] with one input: the joined take and the sample at which each step starts. */
export async function runSteps(
  rig: PrismRig,
  input: Signal,
  list: readonly (readonly [number, Partial<Controls>])[],
): Promise<{ take: Take; marks: number[] }> {
  const takes: Take[] = [];
  const marks: number[] = [];
  const origin = rig.clock;
  for (const [blocks, change] of list) {
    rig.set(change);
    marks.push(rig.clock - origin);
    takes.push(await rig.run(blocks, input));
  }
  return { take: join(takes), marks };
}

export { f2w, w2f };
