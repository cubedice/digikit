/**
 * PRISM and Overloop coupled on the SHARC-only rig (pair-rig.ts): where PRISM's output goes while Overloop runs, the handover
 * between the FX return and Overloop when Overloop starts, stops or comes back, and the transition of the plan's section 4
 * with PRISM sounding. The master stage is its linear case (finding 62). Numbers go to out/runs/prism-transition/measures.txt.
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import { afterAll, describe, expect, test } from 'vitest';
import { OL, type Pattern } from '../../overloop/tests/loop-rig.ts';
import { BLOCK, TRIM, f2w, levelAt, peak, rms, type Signal } from './engine-rig.ts';
import { BOOST, PairRig, differ, type PairTake } from './pair-rig.ts';

const measures: string[] = [];
const note = (line: string): void => void measures.push(line);
afterAll(() => {
  const report = ['PRISM and Overloop coupled:', ...measures].join('\n  ');
  console.info(report);
  mkdirSync('out/runs/prism-transition', { recursive: true });
  writeFileSync('out/runs/prism-transition/measures.txt', `${report}\n`);
});

async function withPair<T>(body: (pair: PairRig) => Promise<T>): Promise<T> {
  const pair = await PairRig.open();
  try {
    return await body(pair);
  } finally {
    await pair.close();
  }
}

// Patterns of eight steps of 320 frames: a cycle is 2560 frames (80 blocks), as in Overloop's rig tests.
const STEP = 320;
const CYCLE = 8 * STEP;
const A: Pattern = { id: 0x47000100, steps: 8, stepFrames: STEP };
const B: Pattern = { id: 0x47000200, steps: 8, stepFrames: STEP };
/** A sound that repeats every cycle exactly: whole numbers of periods of two partials, another mix on the right. */
const periodic =
  (a: number, b: number, amp: number): Signal =>
  (n) => {
    const p = (2 * Math.PI * (((n % CYCLE) + CYCLE) % CYCLE)) / CYCLE;
    return [amp * (0.7 * Math.sin(a * p) + 0.3 * Math.sin(b * p)), amp * (0.4 * Math.sin(a * p) - 0.6 * Math.sin(b * p))];
  };
const SOUND_A = periodic(11, 29, 0.3);
const SOUND_B = periodic(17, 5, 0.25);
const HZ_A = (11 * 48000) / CYCLE;
const HZ_B = (17 * 48000) / CYCLE;
const switchAt =
  (at: number, first: Signal, second: Signal): Signal =>
  (n) =>
    n < at ? first(n) : second(n);
/** PRISM: a synced echo of 960 frames (30 blocks), repeats at half the way to unity. */
const ECHO = { sync: true, syncTime: 960, rpts: 0.5, mix: 1 } as const;

const join = (takes: PairTake[]): PairTake => {
  const n = takes.reduce((s, t) => s + t.left.length, 0);
  const keys = ['left', 'right', 'masterLeft', 'masterRight', 'returnLeft', 'wetLeft', 'tapLeft'] as const;
  const out = Object.fromEntries(keys.map((k) => [k, new Float32Array(n)])) as unknown as PairTake;
  let at = 0;
  for (const t of takes) {
    for (const k of keys) out[k].set(t[k], at);
    at += t.left.length;
  }
  return out;
};
/** The largest difference in [from, to), as a share of y's peak there. */
const relative = (x: ArrayLike<number>, y: ArrayLike<number>, from: number, to: number): number => {
  let worst = 0;
  for (let n = from; n < to; n++) worst = Math.max(worst, Math.abs(x[n]! - y[n]!));
  return worst / Math.max(1e-12, peak(y, from, to));
};

describe('PRISM and Overloop: where PRISM is heard', () => {
  test('the handover between the FX return and Overloop: a crossfade when Overloop starts, one block lost when it faults or stops', async () => {
    // Ten blocks a step. The reference: PRISM on the FX return all the time (Overloop is not loaded). The case: Overloop starts
    // at block 30, faults in block 70 (its adapter plays the master and clears its state) and runs on, and is gone from block 100.
    const plan: ('off' | 'on' | 'fault')[] = [];
    for (let b = 0; b < 130; b++) plan.push(b < 30 || b >= 100 ? 'off' : b === 70 ? 'fault' : 'on');
    const run = async (schedule: ('off' | 'on' | 'fault')[]) =>
      withPair(async (pair) => {
        pair.prism.set(ECHO);
        const takes: PairTake[] = [];
        for (const step of schedule) {
          pair.overloop = step !== 'off';
          takes.push(step === 'fault' ? await pair.fault(SOUND_A) : await pair.run(1, SOUND_A));
        }
        return join(takes);
      });
    const ref = await run(plan.map(() => 'off'));
    const got = await run(plan);
    const at = (block: number) => block * BLOCK;
    // what of PRISM is heard: the output less the dry master
    const dry = Float32Array.from({ length: at(130) }, (_, n) => Math.fround((SOUND_A(n) as readonly [number, number])[0]));
    const heard = (t: PairTake) => Float32Array.from(t.left, (v, n) => v - dry[n]!);
    const g = heard(got);
    const r = heard(ref);
    expect(differ(got.left, ref.left, 0, at(30)), 'before Overloop: the same').toBe(0);
    // Overloop starts: its first block notes PRISM's count, PRISM notices Overloop in the next and moves over 256 frames
    const start = relative(got.left, ref.left, at(30), at(70));
    expect(start, 'nothing lost and nothing twice').toBeLessThan(1e-6);
    expect(peak(got.returnLeft, at(31) + 256, at(70)), 'then nothing of PRISM on the FX return').toBe(0);
    // the fault, block 70: PRISM's block went to the mailbox and is not heard (the one lost block); in block 71 PRISM is back on
    // the return, faded in over the block; Overloop runs again with a new state and PRISM moves over again from block 72
    const lost = rms(r, at(70), at(71));
    expect(peak(g, at(70), at(71))).toBeLessThan(1e-6);
    for (let k = 0; k < BLOCK; k++)
      expect(f2w(got.returnLeft[at(71) + k]!)).toBe(f2w(Math.fround(ref.returnLeft[at(71) + k]! * ((k + 1) / 32))));
    const again = relative(got.left, ref.left, at(72), at(100));
    expect(again).toBeLessThan(1e-6);
    // gone from block 100: the same, one block lost, a fade in, then the FX return word for word
    expect(peak(g, at(100), at(101))).toBeLessThan(1e-6);
    for (let k = 0; k < BLOCK; k++)
      expect(f2w(got.returnLeft[at(101) + k]!)).toBe(f2w(Math.fround(ref.returnLeft[at(101) + k]! * ((k + 1) / 32))));
    expect(differ(got.left, ref.left, at(102), at(130))).toBe(0);
    // never twice: no sample of PRISM is louder than on the return alone
    let louder = 0;
    for (let n = 0; n < g.length; n++) if (Math.abs(g[n]!) > Math.abs(r[n]!) * (1 + 1e-5) + 1e-7) louder += 1;
    expect(louder).toBe(0);
    note(
      `handover: Overloop starts, largest difference to PRISM on the return ${start.toExponential(1)} of the peak; it faults: one block without PRISM (RMS of what was lost ${lost.toFixed(4)}), the next fades in on the return over 32 samples, then it moves over again (${again.toExponential(1)}); it is gone: the same, then the return word for word; no sample of PRISM louder than on the return alone`,
    );
  });

  test('the transition: at the armed change the output goes on unchanged with PRISM ringing; LOOP feeds PRISM the loop; LIVE brings B in dry; G brings B to PRISM', async () => {
    // one echo, so PRISM's tape holds exactly what PRISM took in (the feedback is 0)
    const ONE = { ...ECHO, rpts: 0 };
    const change = 3 * CYCLE;
    const reference = await withPair(async (pair) => {
      pair.prism.set(ONE);
      pair.loop.start(A);
      return pair.run((change + CYCLE) / BLOCK, SOUND_A);
    });
    await withPair(async (pair) => {
      pair.prism.set(ONE);
      pair.loop.start(A);
      const dry = switchAt(change, SOUND_A, SOUND_B);
      // ARM in the last cycle of A, B at its end. PRISM sees the new loop in the block after the change and takes it at once;
      // the page sets INPT to LOOP in its next pass, here ten blocks later
      const lead = await pair.run((change - 3 * STEP) / BLOCK, dry);
      pair.loop.arm();
      pair.loop.changeAtEnd(B);
      const arrive = await pair.run((3 * STEP) / BLOCK, dry);
      const first = await pair.run(10, dry);
      pair.prism.set({ input: 1 });
      const rest = await pair.run((2 * CYCLE) / BLOCK - 10, dry);
      const held = join([first, rest]);
      expect(await pair.loop.status()).toMatchObject({ loop: true, count: 1, loopFrames: CYCLE });
      const loopBuffer = (1 - (await pair.loop.state(OL.OL_ROLL!))[0]!) as 0 | 1;
      const loopWords = await pair.loop.buffer(loopBuffer);
      const t1 = join([lead, arrive, held]);
      // 1. what is heard does not change at the change: up to the first echo of what PRISM took in after it, the output is
      //    that of a run without any change, word for word (the loop is pattern A; PRISM's echo rings on at its level)
      const window =
        differ(t1.left, reference.left, change - CYCLE, change + 960 - BLOCK) +
        differ(t1.right, reference.right, change - CYCLE, change + 960 - BLOCK);
      expect(window, 'from a cycle before the change to the first echo of the loop: the same words').toBe(0);
      // PRISM's part of what is heard there (the same words, so the reference's: its output less its master)
      const ringing = rms(
        Float32Array.from(reference.left, (v, n) => v - reference.masterLeft[n]!),
        change,
        change + 960 - BLOCK,
      );
      expect(ringing).toBeGreaterThan(0.001);
      // 2. LOOP: PRISM takes the loop's tap of the block before times the trim, and nothing of the tracks (pattern B)
      const tape = await pair.prism.tapeLeft(0, pair.clock);
      let fed = 0;
      for (let n = change + BLOCK; n < change + 2 * CYCLE; n++) if (f2w(tape[n]!) !== f2w(Math.fround(t1.tapLeft[n]! * TRIM))) fed += 1;
      expect(fed, 'from the block after the change PRISM takes the loop times the trim, bit for bit').toBe(0);
      // the block of the change: PRISM ran before Overloop took the loop and heard the tracks, pattern B's first 32 frames
      let leak = 0;
      for (let n = change; n < change + BLOCK; n++) if (f2w(tape[n]!) !== f2w(Math.fround((dry(n) as readonly [number, number])[0] / BOOST))) leak += 1;
      expect(leak, 'the one block of the change is the send bus').toBe(0);
      const from = change + CYCLE / 2;
      const fromA = levelAt(tape, HZ_A, from, from + CYCLE);
      const fromB = levelAt(tape, HZ_B, from, from + CYCLE);
      expect(fromA).toBeGreaterThan(0.01);
      expect(fromB / fromA, 'pattern B is not in what PRISM takes').toBeLessThan(1e-4);
      // 3. the crossfader to LIVE: B comes in dry; the loop and its feed into PRISM fade together
      pair.loop.setXfad(0);
      const live = await pair.run((2 * CYCLE) / BLOCK, dry);
      const tape2 = await pair.prism.tapeLeft(0, pair.clock);
      const end = change + 4 * CYCLE;
      expect(peak(tape2, end - CYCLE, end), 'at LIVE the loop feeds PRISM nothing').toBe(0);
      const heardB = levelAt(live.left, HZ_B, live.left.length - CYCLE, live.left.length);
      // B alone and dry: what PRISM still gives is the slow tail of its DC blocker
      const tail = relative(live.left, live.masterLeft, live.left.length - CYCLE, live.left.length);
      expect(tail, 'B alone and dry').toBeLessThan(1e-3);
      // 4. INPT to LIVE (G): PRISM takes pattern B by its sends
      pair.prism.set({ input: 0 });
      await pair.run((2 * CYCLE) / BLOCK, dry);
      const tape3 = await pair.prism.tapeLeft(0, pair.clock);
      const sendB = levelAt(tape3, HZ_B, end + CYCLE / 2, end + CYCLE / 2 + CYCLE);
      expect(sendB).toBeGreaterThan(0.01);
      // nothing feeds back: the held loop's buffer was never written
      const after = await pair.loop.buffer(loopBuffer);
      expect(
        after.every((w, i) => w === loopWords[i]),
        'the loop is not written',
      ).toBe(true);
      note(
        `transition: from a cycle before the armed change to PRISM's first echo of the loop the output is the run without a change, word for word (PRISM's echo there adds ${ringing.toFixed(4)} RMS); PRISM takes the loop from the block after the change (it heard pattern B in that one block), the tap times ${TRIM.toFixed(4)} bit for bit, pattern B in it ${(20 * Math.log10(Math.max(1e-12, fromB / fromA))).toFixed(0)} dB below A; at LIVE the feed is 0 and B is heard alone and dry (${heardB.toFixed(3)}; PRISM's DC blocker tail ${tail.toExponential(1)} of the peak); INPT LIVE: B in PRISM's tape at ${sendB.toFixed(3)}; the loop's buffer unchanged`,
      );
    });
  });

  test('INPT at LIVE while the crossfader stands at LOOP: pattern B reaches PRISM by its sends and its echoes are heard over the loop', async () => {
    await withPair(async (pair) => {
      const change = 3 * CYCLE;
      pair.prism.set(ECHO);
      pair.loop.start(A);
      const dry = switchAt(change, SOUND_A, SOUND_B);
      await pair.run((change - 3 * STEP) / BLOCK, dry);
      pair.loop.arm();
      pair.loop.changeAtEnd(B);
      await pair.run((3 * STEP) / BLOCK, dry);
      // the page puts INPT at LOOP with the new loop; the owner presses G and puts it back at LIVE
      await pair.run(10, dry);
      pair.prism.set({ input: 1 });
      await pair.run(10, dry);
      pair.prism.set({ input: 0 });
      const held = await pair.run((2 * CYCLE) / BLOCK, dry);
      expect((await pair.loop.status()).loop).toBe(true);
      // the output: the loop (pattern A) and PRISM's echoes of pattern B
      const b = levelAt(held.left, HZ_B, held.left.length - 2048, held.left.length);
      const a = levelAt(held.left, HZ_A, held.left.length - 2048, held.left.length);
      expect(b).toBeGreaterThan(0.01);
      note(`INPT LIVE at LOOP: pattern B's echoes are heard over the loop (B ${b.toFixed(3)}, A ${a.toFixed(3)})`);
    });
  });
});
