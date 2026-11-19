/**
 * The two effects of a transition together on the SHARC-only rig, coupled block by block as the Digitakt II runs them:
 * PRISM's block first (a send effect: its output block goes to the FX return), then the master stage, then Overloop's block
 * (the master insert) with that master. The rig carries PRISM's wet mailbox to Overloop, and Overloop's tap and its count
 * LW_TAKEN back to PRISM for its next block. transition.rig.test.ts and transition-audio.ts run it.
 *
 * The master stage is its linear case (finding 62: the compressor unmixed, nothing near the clip, the overdrive off): master
 * = the dry master + the +10 dB stage times the volume ramp (here 1) times the FX return. The dry master is given at the
 * master's level; PRISM's send bus is the tracks before it, so the rig gives PRISM the dry master divided by the +10 dB stage
 * (every track at full send).
 */
import { LoopRig, OL } from '../../overloop/tests/loop-rig.ts';
import { BLOCK, PrismRig, f2w, w2f, type Signal } from './engine-rig.ts';

export const BOOST = w2f(OL.OL_MASTER_BOOST!);

export interface PairTake {
  /** what is heard: Overloop's output, or the master when Overloop does not run */
  left: Float32Array;
  right: Float32Array;
  /** the master stage's output as Overloop received it */
  masterLeft: Float32Array;
  masterRight: Float32Array;
  /** PRISM's output block (the FX return) and what it put in the wet mailbox */
  returnLeft: Float32Array;
  wetLeft: Float32Array;
  /** the tap PRISM received for each block (Overloop's of the block before) */
  tapLeft: Float32Array;
}

export class PairRig {
  readonly prism: PrismRig;
  readonly loop: LoopRig;
  /** Overloop runs (false: it is not loaded or was switched off; the master is heard as it is). A fault is a real one (fault()). */
  overloop = true;
  clock = 0;
  private taken = 0;
  /** the left side of the tap Overloop left after its last block (PRISM reads it in the next) */
  private lastTap = new Float32Array(BLOCK);

  private constructor(prism: PrismRig, loop: LoopRig) {
    this.prism = prism;
    this.loop = loop;
  }

  /** FRAMES: of PRISM's tape and of each of Overloop's buffers (default 65536 and 16384; the device leases 1032192). */
  static async open(o: { prismFrames?: number; loopFrames?: number } = {}): Promise<PairRig> {
    const prism = await PrismRig.open({ frames: o.prismFrames ?? 65536 });
    const loop = await LoopRig.open({ faults: true, frames: o.loopFrames });
    loop.setMaster(1, 0);
    return new PairRig(prism, loop);
  }

  /** The next block Overloop faults (its lease descriptor is broken for that block): the adapter plays the master and clears
   * Overloop's state, as it does on the device. */
  async fault(dry: Signal): Promise<PairTake> {
    this.loop.pokeLoopLease(0, 0);
    const t = await this.run(1, dry);
    this.loop.pokeLoopLease(0, 0x4c425546);
    return t;
  }

  async close(): Promise<void> {
    await this.prism.close();
    await this.loop.close();
  }

  /** `blocks` blocks of the dry master `dry` (the tracks at the master's level). */
  async run(blocks: number, dry: Signal): Promise<PairTake> {
    const n = blocks * BLOCK;
    const t: PairTake = {
      left: new Float32Array(n),
      right: new Float32Array(n),
      masterLeft: new Float32Array(n),
      masterRight: new Float32Array(n),
      returnLeft: new Float32Array(n),
      wetLeft: new Float32Array(n),
      tapLeft: new Float32Array(n),
    };
    const pair = (v: number | readonly [number, number]): readonly [number, number] => (typeof v === 'number' ? [v, v] : v);
    const send: Signal = (k) => {
      const [l, r] = pair(dry(k));
      return [Math.fround(l / BOOST), Math.fround(r / BOOST)];
    };
    for (let b = 0; b < blocks; b++) {
      const at = this.clock;
      const seq = this.prism.wetSeq;
      const p = await this.prism.run(1, send);
      const wrote = this.prism.wetSeq !== seq;
      const ml = new Float32Array(BLOCK);
      const mr = new Float32Array(BLOCK);
      for (let k = 0; k < BLOCK; k++) {
        const [l, r] = pair(dry(at + k));
        ml[k] = Math.fround(Math.fround(l) + Math.fround(p.left[k]! * BOOST));
        mr[k] = Math.fround(Math.fround(r) + Math.fround(p.right[k]! * BOOST));
        t.masterLeft[b * BLOCK + k] = ml[k]!;
        t.masterRight[b * BLOCK + k] = mr[k]!;
        t.returnLeft[b * BLOCK + k] = p.left[k]!;
        t.wetLeft[b * BLOCK + k] = p.wetLeft[k]!;
        t.tapLeft[b * BLOCK + k] = this.lastTap[k]!;
      }
      if (this.overloop) {
        this.loop.wet = {
          signal: (k) => [p.wetLeft[k - at]!, p.wetRight[k - at]!],
          stale: !wrote,
        };
        const o = await this.loop.run(1, (k) => [ml[k - at]!, mr[k - at]!]);
        t.left.set(o.left, b * BLOCK);
        t.right.set(o.right, b * BLOCK);
        this.taken = await this.loop.taken();
        const tap = await this.loop.tapWords();
        this.prism.pokeLoop(tap, this.taken);
        for (let k = 0; k < BLOCK; k++) this.lastTap[k] = w2f(tap[4 + k]!);
      } else {
        // the master is heard as it is; Overloop's clock stands still, its count does not move
        t.left.set(ml, b * BLOCK);
        t.right.set(mr, b * BLOCK);
        this.loop.clock += BLOCK;
      }
      this.clock += BLOCK;
    }
    return t;
  }
}

/** The samples whose words differ in [from, to). */
export function differ(x: ArrayLike<number>, y: ArrayLike<number>, from = 0, to = Math.min(x.length, y.length)): number {
  let count = 0;
  for (let n = from; n < to; n++) if (f2w(x[n]!) !== f2w(y[n]!)) count += 1;
  return count;
}
