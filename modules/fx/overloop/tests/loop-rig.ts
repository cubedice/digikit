/**
 * Overloop's engine on the SHARC-only rig: the assembled source behind the production effect adapter (core/fx) as a master
 * effect, with stand-ins for the storage descriptors, the position record, the status mailbox, the tap, the wet mailbox and the
 * master stage's volume ramp (provider.s).
 * loop.rig.test.ts and render-audio.ts run it.
 *
 *     const rig = await LoopRig.open({ frames: 16384 });
 *     rig.start(A);                                  // the sequencer: pattern A plays from its first step
 *     await rig.run(200, input);                     // 200 blocks: take.left, take.right (the master after the effect)
 *     rig.arm(); rig.changeAtEnd(B);                 // ARM, and pattern B at the end of A's cycle
 *
 * The rig's sequencer writes the position record the way core/pattern-position does: a boundary is counted in the record
 * of the block it begins with. The control home is played by the rig as well: it hands the crossfader and the counters
 * to the engine as parameter words and, like core/loop-control, sets the crossfader to LOOP when the status shows a new
 * loop (`home: false` leaves that out).
 *
 * Every block is held to the rules of the effect contract: the hook returns, the context and the registers come back, the
 * adapter counts no fault and the output is finite. A case that expects a fault opens the rig with `faults: true`.
 */
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { FxRig, P0, SW_ALIAS_BASE, f2w, w2f } from '../../../../packages/sharc-rig-kit/src/index.ts';

export const FS = 48000;
export const BLOCK = 32;
/** The stand-in buffers (external memory, as the device leases them). */
export const BUFFERS = [0x9aa2b9c8, 0x9b22b9c8] as const;
/** Frames of a buffer as the device leases the two: what the smaller one holds. */
export const DEVICE_FRAMES = 1032192;
const HERE = import.meta.dirname;
const POISON = 0x7fc00001;

/** The numbers of a C-style header: `#define NAME value` lines. */
function defines(file: string): Record<string, number> {
  return Object.fromEntries(
    [...readFileSync(file, 'utf8').matchAll(/^#define (\w+) (0x[0-9a-fA-F]+|\d+)$/gm)].map((m) => [m[1]!, Number(m[2])]),
  );
}
/** The engine's state layout and constants (src/overloop.h) and the contract (sdk/dsp/contract/dk_loop.inc). */
export const OL = defines(path.join(HERE, '../src/overloop.h'));
export const LP = defines(path.join(HERE, '../../../../sdk/dsp/contract/dk_loop.inc'));

/** A signal: the two sides' samples at a frame, or one value for both. */
export type Signal = (frame: number) => number | readonly [number, number];
export const silence: Signal = () => 0;

export interface Pattern {
  /** the opaque identity the position record carries */
  id: number;
  /** steps of a cycle */
  steps: number;
  /** frames of each step: a whole number of blocks; one number for every step, or a list the boundaries go through in turn */
  stepFrames: number | readonly number[];
}

export interface Take {
  /** the frame of the rig's clock at the take's first sample */
  start: number;
  /** the master block after the effect */
  left: Float32Array;
  right: Float32Array;
  /** the input, as given */
  dryLeft: Float32Array;
  dryRight: Float32Array;
  /** the tap as the engine left it after each block */
  tapLeft: Float32Array;
  tapRight: Float32Array;
  /** the send effect's output the rig put in the wet mailbox before each block (0 where it wrote none) */
  wetLeft: Float32Array;
  wetRight: Float32Array;
  /** instructions of each hook call (core/fx and the effect) */
  instructions: number[];
  /** blocks in which the adapter counted a fault (only with `faults: true`) */
  faulted: number;
}

export interface LoopRigOptions {
  /** frames of each buffer (a multiple of 32, at least 4096); default 16384 */
  frames?: number;
  /** fill the buffers with NaN words first, so a read of frames that were never recorded shows (default up to 65536 frames) */
  poison?: boolean;
  /** record the stores of the module's code (slower) */
  stores?: boolean;
  /** faults are expected: count them instead of throwing */
  faults?: boolean;
  /** a friendly register context instead of a hostile one for every block */
  friendly?: boolean;
  /** play the control home: set the crossfader to LOOP and acknowledge when the status shows a new loop (default true) */
  home?: boolean;
  /** read the tap after every block (slower) */
  tap?: boolean;
}

/** The send effect as the rig plays it: what it writes to the wet mailbox before each block. */
export interface Wet {
  /** its output (one value for both sides) */
  signal: Signal;
  /** a count that does not move: the block is not new (the send effect did not run) */
  stale?: boolean;
  /** the mailbox's magic word (default the right one) */
  magic?: number;
}

/** The status the engine publishes, decoded (dk_loop.inc LS_*). */
export interface Status {
  heartbeat: number;
  loop: boolean;
  armed: boolean;
  cut: boolean;
  track: boolean;
  nextCut: boolean;
  held: boolean;
  count: number;
  loopFrames: number;
  nextFrames: number;
  phase: number;
  gain: number;
  loopSteps: number;
  steps: number;
  step: number;
  capacity: number;
}

export class LoopRig {
  /** The heaviest hook call (core/fx and the effect) of every rig of this process, and the block it was. */
  static worst = 0;
  static worstIn = '';
  readonly fx: FxRig;
  readonly frames: number;
  /** frames run so far: the argument of the next input sample */
  clock = 0;
  /** the crossfader the home holds, 0 .. 127 */
  xfad = 0;
  /** stores of the module's code outside its own memory, the two buffers and the two mailboxes, as text (with `stores`) */
  readonly foreign: string[] = [];
  private armSeq = 0;
  private dropSeq = 0;
  private seen = 0;
  private faultCount = 0;
  private readonly o: LoopRigOptions;
  /** the send effect, while it runs */
  wet: Wet | undefined;
  private wetSeq = 0;
  // the sequencer
  private running = false;
  private pattern: Pattern | undefined;
  private step = 0;
  private nextBoundary = 0;
  private pending: { pattern: Pattern; atEnd: boolean } | undefined;
  private generation = 0;
  private tx = 0;
  private boundaries = 0;
  /** the frame of the rig's clock at which each boundary began: [frame, pattern id, step] */
  readonly log: [number, number, number][] = [];

  private constructor(fx: FxRig, frames: number, o: LoopRigOptions) {
    this.fx = fx;
    this.frames = frames;
    this.o = o;
  }

  static async open(o: LoopRigOptions = {}): Promise<LoopRig> {
    const frames = o.frames ?? 16384;
    const fx = await FxRig.open([{ pc: '_bl_process', state: '', desc: '_bl_desc', params: [0, 0, 0, 0] }], {
      sources: [path.join(HERE, '../src/dsp/overloop.s'), path.join(HERE, 'provider.s')],
      stepLimit: 400_000,
      recordStores: o.stores ?? false,
    });
    const rig = new LoopRig(fx, frames, o);
    rig.pokeLoopLease(3, 8 * frames);
    rig.pokePhraseLease(4, 8 * frames);
    rig.pokePhraseLease(5, frames);
    if (o.poison ?? frames <= 65536)
      for (const base of BUFFERS) for (let k = 0; k < 2 * frames; k++) fx.pokeExternal(base + 4 * k, POISON);
    rig.writeControls();
    return rig;
  }

  close(): Promise<void> {
    return this.fx.close();
  }

  // ---- the control home ------------------------------------------------------------------------------------------------------

  /** The crossfader, 0 (LIVE) .. 127 (LOOP). */
  setXfad(value: number): void {
    this.xfad = value;
    this.writeControls();
  }

  /** ARM: arms the engine, or disarms it when it is armed. */
  arm(): void {
    this.armSeq = (this.armSeq + 1) & 127;
    this.writeControls();
  }

  drop(): void {
    this.dropSeq = (this.dropSeq + 1) & 127;
    this.writeControls();
  }

  /** The parameter words as the fx service makes them from the home's raw values: min + span * raw / 127. */
  private writeControls(): void {
    const words = [this.xfad / 127, 127 * (this.armSeq / 127), 127 * (this.dropSeq / 127), 127 * (this.seen / 127)];
    words.forEach((value, j) => this.fx.setRowWord(0, P0 + j, f2w(Math.fround(value))));
  }

  /** One raw parameter word (a case with a value the home never sends). */
  pokeParam(index: number, bits: number): void {
    this.fx.setRowWord(0, P0 + index, bits);
  }

  pokeLoopLease(word: number, value: number): void {
    this.fx.poke(this.fx.built.data._loop_buffer_info! + 4 * word, value);
  }

  pokePhraseLease(word: number, value: number): void {
    this.fx.poke(this.fx.built.data._phrase_storage_info! + 4 * word, value);
  }

  /** The master stage's volume ramp: the gain of the block's first sample and the step a sample (floats). */
  setMaster(start: number, step = 0): void {
    this.fx.poke(this.fx.built.data._fw_master_gain!, f2w(Math.fround(start)));
    this.fx.poke(this.fx.built.data._fw_master_step!, f2w(Math.fround(step)));
  }

  /** The tap's words as they are (LT_WORDS). */
  async tapWords(): Promise<number[]> {
    return (await this.fx.peek(this.fx.built.data._loop_tap!, LP.LT_WORDS!)).map((x) => x ?? 0);
  }

  /** The wet mailbox's count of blocks the engine finished (LW_TAKEN). */
  async taken(): Promise<number> {
    return (await this.fx.peek(this.fx.built.data._loop_wet! + 4 * LP.LW_TAKEN!, 1))[0] ?? 0;
  }

  // ---- the sequencer -----------------------------------------------------------------------------------------------------------

  /** START: `pattern` plays from its first step with the next block. */
  start(pattern: Pattern): void {
    this.running = true;
    this.pattern = pattern;
    this.pending = undefined;
    this.step = -1;
    this.generation += 1;
    this.nextBoundary = this.clock;
  }

  stop(): void {
    this.running = false;
    this.pending = undefined;
  }

  /** The pattern changes at the end of the cycle that plays (a sequential change). */
  changeAtEnd(pattern: Pattern): void {
    this.pending = { pattern, atEnd: true };
  }

  /** The pattern changes at the next step boundary: the new pattern starts at its first step there (a direct change). */
  changeNow(pattern: Pattern): void {
    this.pending = { pattern, atEnd: false };
  }

  /** While stopped: another pattern is selected; it plays from the next START. */
  select(pattern: Pattern): void {
    if (this.running) throw new Error('select: the sequencer runs; use changeAtEnd or changeNow');
    this.pattern = pattern;
  }

  /** The frames of the steps from now on (a tempo change); the step that plays keeps its length. */
  setStepFrames(stepFrames: number | readonly number[]): void {
    if (!this.pattern) throw new Error('no pattern');
    this.pattern = { ...this.pattern, stepFrames };
  }

  private framesOf(pattern: Pattern): number {
    const f = pattern.stepFrames;
    const frames = typeof f === 'number' ? f : f[this.boundaries % f.length]!;
    if (frames % BLOCK) throw new Error(`a step of ${frames} frames is not a whole number of blocks`);
    return frames;
  }

  /** The position record of the block that begins at the rig's clock. */
  private writePosition(valid = true): void {
    if (this.running && this.pattern && this.clock >= this.nextBoundary) {
      let pattern = this.pattern;
      let step = this.step + 1;
      if (step >= pattern.steps) step = 0;
      if (this.pending && (!this.pending.atEnd || step === 0) && this.step >= 0) {
        pattern = this.pending.pattern;
        step = 0;
        this.pending = undefined;
      }
      this.pattern = pattern;
      this.step = step;
      this.nextBoundary = this.clock + this.framesOf(pattern);
      this.boundaries += 1;
      this.log.push([this.clock, pattern.id, step]);
    }
    this.tx += 1;
    const at = this.fx.built.data._pattern_position_info!;
    const words = [
      valid ? 0x50504f53 : 0,
      2,
      this.tx,
      this.boundaries,
      this.generation,
      this.running ? 3 : 0,
      this.pattern?.id ?? 0,
      Math.max(this.step, 0),
      this.pattern?.steps ?? 0,
    ];
    words.forEach((value, k) => this.fx.poke(at + 4 * k, value >>> 0));
  }

  /** One raw word of the position record for the next block only (a case that breaks the stream): applied after the sequencer wrote it. */
  private positionPatch: [number, number][] = [];
  patchPosition(word: number, value: number): void {
    this.positionPatch.push([word, value]);
  }

  // ---- state -------------------------------------------------------------------------------------------------------------------

  async state(word: number, count = 1): Promise<number[]> {
    const words = await this.fx.peek(this.fx.stateAddress(0) + 4 * word, count);
    return words.map((w) => w ?? 0);
  }

  async status(): Promise<Status> {
    const w = (await this.fx.peek(this.fx.built.data._loop_ui_status!, 16)).map((x) => x ?? 0);
    const state = w[LP.LS_STATE!]!;
    const steps = w[LP.LS_STEPS!]!;
    return {
      heartbeat: w[LP.LS_HEARTBEAT!]!,
      loop: (state & LP.LS_LOOP!) !== 0,
      armed: (state & LP.LS_ARMED!) !== 0,
      cut: (state & LP.LS_CUT!) !== 0,
      track: (state & LP.LS_TRACK!) !== 0,
      nextCut: (state & LP.LS_NEXT_CUT!) !== 0,
      held: (state & LP.LS_HELD!) !== 0,
      count: state >>> LP.LS_COUNT_SHIFT!,
      loopFrames: w[LP.LS_LOOP_FRAMES!]!,
      nextFrames: w[LP.LS_NEXT_FRAMES!]!,
      phase: w[LP.LS_PHASE!]!,
      gain: w2f(w[LP.LS_GAIN!]!),
      loopSteps: steps & 255,
      steps: (steps >>> 8) & 255,
      step: (steps >>> 16) & 255,
      capacity: w[LP.LS_CAPACITY!]!,
    };
  }

  /** The 16 status words as they are. */
  async statusWords(): Promise<number[]> {
    return (await this.fx.peek(this.fx.built.data._loop_ui_status!, 16)).map((x) => x ?? 0);
  }

  /** The tap's head: magic, version, block, flags. */
  async tapHead(): Promise<number[]> {
    return (await this.fx.peek(this.fx.built.data._loop_tap!, 4)).map((x) => x ?? 0);
  }

  /** The words of one buffer's planes (null: never written). */
  buffer(index: 0 | 1): Promise<(number | null)[]> {
    return this.fx.peekExternal(BUFFERS[index], 2 * this.frames);
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
      tapLeft: new Float32Array(n),
      tapRight: new Float32Array(n),
      wetLeft: new Float32Array(n),
      wetRight: new Float32Array(n),
      instructions: [],
      faulted: 0,
    };
    const master = Array.from({ length: 2 * BLOCK }, () => 0);
    for (let b = 0; b < blocks; b++) {
      for (let k = 0; k < BLOCK; k++) {
        const v = input(this.clock + k);
        const [l, r] = typeof v === 'number' ? [v, v] : v;
        master[k] = l;
        master[BLOCK + k] = r;
        take.dryLeft[b * BLOCK + k] = l;
        take.dryRight[b * BLOCK + k] = r;
      }
      this.writeWet(take, b);
      this.writePosition();
      for (const [word, value] of this.positionPatch)
        this.fx.poke(this.fx.built.data._pattern_position_info! + 4 * word, value >>> 0);
      this.positionPatch = [];
      const block = await this.fx.block(master, this.o.friendly ? 'friendly' : undefined);
      const where = `block ${this.clock / BLOCK}`;
      if (!block.returned) throw new Error(`${where}: the hook did not return (${block.halt} at pc 0x${block.finalPc.toString(16)})`);
      if (block.ctxBad.length || block.regsBad.length)
        throw new Error(`${where}: registers came back changed: ${[...block.ctxBad, ...block.regsBad].join(' ')}`);
      // the adapter's fault count of the row: a block that raised it played the input
      const faulted = block.faults[0]! !== this.faultCount;
      this.faultCount = block.faults[0]!;
      if (faulted && !this.o.faults) throw new Error(`${where}: the adapter counted a fault`);
      if (faulted) {
        take.faulted += 1;
        if (master.some((v, k) => f2w(block.master[k]!) !== f2w(v))) throw new Error(`${where}: a faulted block did not play the input`);
      }
      for (let k = 0; k < BLOCK; k++) {
        const l = block.master[k]!;
        const r = block.master[BLOCK + k]!;
        if (!Number.isFinite(l) || !Number.isFinite(r)) throw new Error(`${where}: the output is not finite`);
        take.left[b * BLOCK + k] = l;
        take.right[b * BLOCK + k] = r;
      }
      take.instructions.push(block.instructions);
      if (block.instructions > LoopRig.worst) {
        LoopRig.worst = block.instructions;
        LoopRig.worstIn = where;
      }
      if (this.o.stores) this.checkStores(block.stores, where);
      if (this.o.tap) {
        const words = await this.fx.peek(this.fx.built.data._loop_tap! + 4 * LP.LT_LEFT!, 2 * BLOCK);
        for (let k = 0; k < BLOCK; k++) {
          take.tapLeft[b * BLOCK + k] = w2f(words[k] ?? 0);
          take.tapRight[b * BLOCK + k] = w2f(words[BLOCK + k] ?? 0);
        }
      }
      if (this.o.home ?? true) await this.playHome();
      this.clock += BLOCK;
    }
    return take;
  }

  /** The send effect's block in the wet mailbox, as PRISM writes it: the samples, then a new count. */
  private writeWet(take: Take, b: number): void {
    if (!this.wet) return;
    const at = this.fx.built.data._loop_wet!;
    this.fx.poke(at + 4 * LP.LW_MAGIC!, this.wet.magic ?? LP.LW_MAGIC_VALUE!);
    this.fx.poke(at + 4 * LP.LW_VERSION!, LP.LW_VERSION_VALUE!);
    if (this.wet.stale) return;
    for (let k = 0; k < BLOCK; k++) {
      const v = this.wet.signal(this.clock + k);
      const [l, r] = typeof v === 'number' ? [v, v] : v;
      this.fx.poke(at + 4 * (LP.LW_LEFT! + k), f2w(Math.fround(l)));
      this.fx.poke(at + 4 * (LP.LW_RIGHT! + k), f2w(Math.fround(r)));
      take.wetLeft[b * BLOCK + k] = Math.fround(l);
      take.wetRight[b * BLOCK + k] = Math.fround(r);
    }
    this.wetSeq += 1;
    this.fx.poke(at + 4 * LP.LW_SEQ!, this.wetSeq);
  }

  /** What core/loop-control does with the status: a new loop sets the crossfader to LOOP and is acknowledged. */
  private async playHome(): Promise<void> {
    const state = (await this.fx.peek(this.fx.built.data._loop_ui_status! + 4 * LP.LS_STATE!, 1))[0] ?? 0;
    const count = (state >>> LP.LS_COUNT_SHIFT!) & 127;
    if (count !== this.seen) {
      this.seen = count;
      if (state & LP.LS_LOOP!) this.xfad = 127;
      this.writeControls();
    }
  }

  /** The module's code may store to its state, its output block, the two buffers, the status mailbox and the tap; never to the records it reads. */
  private checkStores(stores: [pc: number, address: number, width: number][], where: string): void {
    const built = this.fx.built;
    const state = { address: this.fx.stateAddress(0), bytes: this.fx.descWords('_bl_desc')[3]! };
    const own = [
      state,
      { address: built.data._fx_scratch!, bytes: 256 },
      { address: built.data._loop_ui_status!, bytes: 64 },
      { address: built.data._loop_tap!, bytes: 272 },
      // of the wet mailbox only its own count
      { address: built.data._loop_wet! + 4 * LP.LW_TAKEN!, bytes: 4 },
      ...BUFFERS.map((address) => ({ address, bytes: 8 * this.frames })),
    ];
    for (const [pc, at, width] of stores) {
      if (!built.moduleCode.some(([lo, hi]) => lo <= pc && pc < hi)) continue;
      // the core keeps internal memory at the loader alias
      const address = SW_ALIAS_BASE <= at && at < 0x80000000 ? at - SW_ALIAS_BASE : at;
      if (own.some((r) => r.address <= address && address + width <= r.address + r.bytes)) continue;
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

/** The largest difference between x[from + k] and y[at + k] over `length` samples. */
export function maxDifference(x: ArrayLike<number>, from: number, y: ArrayLike<number>, at: number, length: number): number {
  let m = 0;
  for (let k = 0; k < length; k++) m = Math.max(m, Math.abs(x[from + k]! - y[at + k]!));
  return m;
}

/** Two takes in a row as one. */
export function join(...takes: Take[]): Take {
  const n = takes.reduce((sum, t) => sum + t.left.length, 0);
  const out: Take = {
    start: takes[0]!.start,
    left: new Float32Array(n),
    right: new Float32Array(n),
    dryLeft: new Float32Array(n),
    dryRight: new Float32Array(n),
    tapLeft: new Float32Array(n),
    tapRight: new Float32Array(n),
    wetLeft: new Float32Array(n),
    wetRight: new Float32Array(n),
    instructions: [],
    faulted: 0,
  };
  let at = 0;
  for (const t of takes) {
    out.left.set(t.left, at);
    out.right.set(t.right, at);
    out.dryLeft.set(t.dryLeft, at);
    out.dryRight.set(t.dryRight, at);
    out.tapLeft.set(t.tapLeft, at);
    out.tapRight.set(t.tapRight, at);
    out.wetLeft.set(t.wetLeft, at);
    out.wetRight.set(t.wetRight, at);
    out.instructions.push(...t.instructions);
    out.faulted += t.faulted;
    at += t.left.length;
  }
  return out;
}
