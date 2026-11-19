/**
 * Overloop's engine on the SHARC-only rig, behind the production effect adapter as a master effect (docs/findings/360).
 * Every claim of the engine has a number here. The click measure is the largest second difference of the output around an
 * event, against the same measure in a steady stretch of the same take (docs/findings/354 "Clicks").
 */
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { afterAll, describe, expect, test } from 'vitest';
import { f2w, w2f } from '../../../../packages/sharc-rig-kit/src/index.ts';
import {
  BLOCK,
  DEVICE_FRAMES,
  FS,
  LP,
  LoopRig,
  OL,
  jitter,
  join,
  maxDifference,
  maxSecondDifference,
  peak,
  rms,
  type LoopRigOptions,
  type Pattern,
  type Signal,
  type Take,
} from './loop-rig.ts';

const DECLARED = Number(
  /^dsp_cost_fx\s*=\s*(\d+)/m.exec(readFileSync(path.join(import.meta.dirname, '../module.toml'), 'utf8'))?.[1],
);

/** The numbers of this run, printed at the end and kept in out/runs/overloop-engine/measures.txt. */
const measures: string[] = [];
const note = (line: string): void => void measures.push(line);
afterAll(() => {
  const report = ['Overloop engine measures:', ...measures, `heaviest block: ${LoopRig.worst} instructions (${LoopRig.worstIn})`].join(
    '\n  ',
  );
  console.info(report);
  mkdirSync('out/runs/overloop-engine', { recursive: true });
  writeFileSync('out/runs/overloop-engine/measures.txt', `${report}\n`);
});

async function withRig<T>(o: LoopRigOptions, body: (rig: LoopRig) => Promise<T>): Promise<T> {
  const rig = await LoopRig.open(o);
  try {
    return await body(rig);
  } finally {
    await rig.close();
  }
}

// Patterns of the rig's sequencer. A cycle of A and of B is 2560 frames (80 blocks), so the seam has its full 480 frames.
const STEP = 320;
const A: Pattern = { id: 0x47000100, steps: 8, stepFrames: STEP };
const B: Pattern = { id: 0x47000200, steps: 8, stepFrames: STEP };
const C: Pattern = { id: 0x47000300, steps: 8, stepFrames: STEP };
const CYCLE = 8 * STEP;

/**
 * A sound that repeats every `cycle` frames exactly: two partials with whole numbers of periods in a cycle, the right side
 * another mix of them. A function of the frame modulo the cycle, so two cycles are the same samples.
 */
function periodic(cycle: number, a: number, b: number, amp: number, origin = 0): Signal {
  return (n) => {
    const p = (((n - origin) % cycle) + cycle) % cycle;
    const x = Math.sin((2 * Math.PI * a * p) / cycle);
    const y = Math.sin((2 * Math.PI * b * p) / cycle);
    return [amp * (0.7 * x + 0.3 * y), amp * (0.4 * x - 0.6 * y)];
  };
}
/** `first` up to frame `at`, `second` from there. */
const switchAt =
  (at: number, first: Signal, second: Signal): Signal =>
  (n) =>
    n < at ? first(n) : second(n);
const SOUND_A = periodic(CYCLE, 11, 29, 0.3);
const SOUND_B = periodic(CYCLE, 17, 5, 0.25);
const SOUND_C = periodic(CYCLE, 7, 41, 0.2);

/** x times a factor, sample by sample in single precision. */
const scaled = (x: ArrayLike<number>, factor: number): Float32Array => Float32Array.from(x, (v) => Math.fround(v * factor));
const blocks = (frames: number): number => {
  if (frames % BLOCK) throw new Error(`${frames} frames are not whole blocks`);
  return frames / BLOCK;
};
const sameBits = (x: ArrayLike<number>, y: ArrayLike<number>, from = 0, to = x.length): number => {
  let differ = 0;
  for (let n = from; n < to; n++) if (f2w(x[n]!) !== f2w(y[n]!)) differ += 1;
  return differ;
};
/** The click measure around `at` against the steady stretch [steadyFrom, steadyTo). */
const click = (x: ArrayLike<number>, at: number, steadyFrom: number, steadyTo: number): number =>
  maxSecondDifference(x, at - 64, at + 64).value / maxSecondDifference(x, steadyFrom, steadyTo).value;

/**
 * Pattern A for `cycles` cycles from the rig's clock (which must be a boundary of A's cycle), armed in the last one, then a
 * sequential change to B and `turns` cycles of B. Gives the take and the frame of the change in it.
 */
async function armedChange(rig: LoopRig, cycles: number, turns: number, armAfterSteps = 3): Promise<{ take: Take; change: number }> {
  const origin = rig.clock;
  const change = cycles * CYCLE;
  const input = switchAt(origin + change, SOUND_A, SOUND_B);
  rig.start(A);
  const before = await rig.run(blocks((cycles - 1) * CYCLE + armAfterSteps * STEP), input);
  rig.arm();
  rig.changeAtEnd(B);
  const after = await rig.run(blocks((8 - armAfterSteps) * STEP + turns * CYCLE), input);
  return { take: join(before, after), change };
}

describe('Overloop engine: an armed change at the pattern end', () => {
  test('the loop is the cycle that just ended, exactly, and what is heard goes on as one cycle earlier', async () => {
    await withRig({ tap: true }, async (rig) => {
      const { take, change } = await armedChange(rig, 4, 5);
      const status = await rig.status();
      expect(status).toMatchObject({ loop: true, armed: false, cut: false, count: 1, loopFrames: CYCLE, loopSteps: 8 });
      // five turns of the loop: each sample is the one a cycle before it, on both sides
      const turns = 5 * CYCLE;
      const left = maxDifference(take.left, change, take.left, change - CYCLE, turns);
      const right = maxDifference(take.right, change, take.right, change - CYCLE, turns);
      expect(left, 'left: the output after the change against one cycle earlier').toBe(0);
      expect(right).toBe(0);
      // and it is A that goes on, not B: the input after the change is B
      expect(maxDifference(take.left, change, take.dryLeft, change - CYCLE, CYCLE)).toBe(0);
      expect(rms(take.dryLeft, change, change + CYCLE)).toBeGreaterThan(0.05);
      expect(maxDifference(take.left, change, take.dryLeft, change, CYCLE)).toBeGreaterThan(0.1);
      // the click measure at the boundary and at every turn, and the level of every turn
      const clicks = [0, 1, 2, 3, 4].map((turn) => click(take.left, change + turn * CYCLE, change - CYCLE + 200, change - 200));
      const levels = [0, 1, 2, 3, 4].map(
        (turn) => rms(take.left, change + turn * CYCLE, change + (turn + 1) * CYCLE) / rms(take.left, change - CYCLE, change),
      );
      note(
        `sequential change, cycle ${CYCLE} frames: loop ${status.loopFrames} frames; largest difference to one cycle earlier over 5 turns ${left} (left) ${right} (right); click at the boundary and the turns ${clicks.map((c) => c.toFixed(2)).join(' ')} x steady; level of each turn ${levels.map((l) => l.toFixed(6)).join(' ')} of the cycle before`,
      );
      for (const c of clicks) expect(c).toBeLessThanOrEqual(1.001);
      for (const l of levels) expect(l).toBe(1);
      // the tap is the loop at the crossfader's level, at the level before the master stage: here the whole output from the
      // change on divided by the master's gain after its sum (+10 dB, the ramp at 1), nothing before it
      const inv = w2f((await rig.state(OL.OL_INV!))[0]!);
      expect(Math.abs(inv * w2f(OL.OL_MASTER_BOOST!) - 1)).toBeLessThan(1e-6);
      expect(peak(take.tapLeft, 0, change)).toBe(0);
      expect(maxDifference(take.tapLeft, change, scaled(take.left, inv), change, turns)).toBe(0);
      expect(maxDifference(take.tapRight, change, scaled(take.right, inv), change, turns)).toBe(0);
      const head = await rig.tapHead();
      expect(head.slice(0, 2)).toEqual([0x4c544150, 1]);
      expect(head[3], 'the tap carries the loop, and the count of loops').toBe(1 | (1 << LP.LT_COUNT_SHIFT!));
      // its block count is the adapter's: one more with every block
      await rig.run(1, SOUND_B);
      expect((await rig.tapHead())[2]! - head[2]!).toBe(1);
    });
  });

  test('steps of unequal length: the loop has the frames of the last cycle as it was played', async () => {
    await withRig({}, async (rig) => {
      // three step lengths over eight steps: no two cycles in a row have the same length, as with a tempo that is no whole number of blocks a step
      const uneven: Pattern = { id: A.id, steps: 8, stepFrames: [320, 352, 288] };
      rig.start(uneven);
      await rig.run(250, SOUND_A);
      rig.arm();
      rig.changeAtEnd(B);
      await rig.run(400, SOUND_A);
      const starts = rig.log.filter(([, , step]) => step === 0).map(([frame]) => frame);
      const changeAt = rig.log.find(([, id]) => id === B.id)![0];
      const last = changeAt - starts.filter((frame) => frame < changeAt).at(-1)!;
      const status = await rig.status();
      const cycles = starts.slice(1).map((s, i) => s - starts[i]!);
      note(`steps of 320, 352, 288 frames in turn: the cycles were ${cycles.join(', ')} frames; the loop has ${status.loopFrames}`);
      expect(new Set(cycles).size, 'the cycles differ').toBeGreaterThan(1);
      expect(status.loop).toBe(true);
      expect(status.loopFrames, 'the frames from the last first step to the change').toBe(last);
    });
  });

  test('ARM without a change does nothing, and a second ARM takes it back', async () => {
    await withRig({}, async (rig) => {
      rig.start(A);
      await rig.run(blocks(CYCLE), SOUND_A);
      rig.arm();
      const armed = await rig.run(blocks(3 * CYCLE), SOUND_A);
      let status = await rig.status();
      expect(status).toMatchObject({ armed: true, loop: false, count: 0, track: true, nextFrames: CYCLE, steps: 8 });
      expect(sameBits(armed.left, armed.dryLeft) + sameBits(armed.right, armed.dryRight), 'armed and waiting: the input bit for bit').toBe(0);
      rig.arm();
      rig.changeAtEnd(B);
      const disarmed = await rig.run(blocks(2 * CYCLE), SOUND_A);
      status = await rig.status();
      expect(status).toMatchObject({ armed: false, loop: false, count: 0 });
      expect(sameBits(disarmed.left, disarmed.dryLeft)).toBe(0);
    });
  });

  test('without the home the crossfader stays at LOOP; the home takes it over when it has seen the loop', async () => {
    await withRig({ home: false }, async (rig) => {
      const { take, change } = await armedChange(rig, 3, 2);
      // the home's crossfader is still at LIVE (0): the engine holds LOOP by itself
      expect((await rig.status()).held).toBe(true);
      expect(maxDifference(take.left, change, take.left, change - CYCLE, 2 * CYCLE)).toBe(0);
      // the acknowledgement with the crossfader at LOOP, as core/loop-control sends it
      rig.pokeParam(0, f2w(1));
      rig.pokeParam(3, f2w(1));
      await rig.run(2, SOUND_B);
      const status = await rig.status();
      expect(status.held).toBe(false);
      expect(status.gain).toBe(1);
    });
  });
});

describe('Overloop engine: the crossfader', () => {
  test('LIVE is the input bit for bit, LOOP the loop alone, and between them the law', async () => {
    await withRig({}, async (rig) => {
      const { take, change } = await armedChange(rig, 3, 1);
      const origin = change - CYCLE;
      const loopAt = (frame: number, side: Float32Array): number => side[origin + ((frame - change) % CYCLE)]!;
      // LOOP: the loop alone, while B plays into the effect
      const atLoop = await rig.run(blocks(CYCLE), SOUND_B);
      let worst = 0;
      for (let k = 0; k < atLoop.left.length; k++) worst = Math.max(worst, Math.abs(atLoop.left[k]! - loopAt(atLoop.start + k, take.left)));
      expect(worst, 'at LOOP the output is the loop').toBe(0);
      expect((await rig.status()).gain).toBe(1);
      // half way: output = input + (loop - input) * g in single precision, once the crossfader has arrived
      rig.setXfad(64);
      const moving = await rig.run(blocks(CYCLE), SOUND_B);
      const g = Math.fround(64 / 127);
      const half = await rig.run(blocks(CYCLE), SOUND_B);
      let differ = 0;
      for (let k = 0; k < half.left.length; k++) {
        for (const [out, dry, loop] of [
          [half.left, half.dryLeft, take.left],
          [half.right, half.dryRight, take.right],
        ] as const) {
          const l = loopAt(half.start + k, loop);
          const want = Math.fround(dry[k]! + Math.fround(Math.fround(l - dry[k]!) * g));
          if (f2w(out[k]!) !== f2w(want)) differ += 1;
        }
      }
      expect(differ, 'between LIVE and LOOP: the law, bit for bit').toBe(0);
      expect((await rig.status()).gain).toBe(g);
      // the move itself: 1/256 a frame, no step in the output
      const moved = Math.ceil((1 - g) * 256);
      const moveClick = maxSecondDifference(moving.left, 0, moved + 64).value / maxSecondDifference(half.left, 200, CYCLE - 200).value;
      // LIVE: the input as it is, with the loop still held
      rig.setXfad(0);
      await rig.run(blocks(STEP), SOUND_B);
      const live = await rig.run(blocks(CYCLE), SOUND_B);
      const status = await rig.status();
      expect(sameBits(live.left, live.dryLeft) + sameBits(live.right, live.dryRight), 'at LIVE the input bit for bit').toBe(0);
      expect(status).toMatchObject({ loop: true, gain: 0, loopFrames: CYCLE });
      // back to LOOP: the loop is still there and still in its phase
      rig.setXfad(127);
      await rig.run(blocks(STEP), SOUND_B);
      const back = await rig.run(blocks(CYCLE), SOUND_B);
      worst = 0;
      for (let k = 0; k < back.left.length; k++) worst = Math.max(worst, Math.abs(back.left[k]! - loopAt(back.start + k, take.left)));
      expect(worst, 'the loop stays ready at LIVE and keeps its phase').toBe(0);
      note(
        `crossfader: LOOP the loop alone (difference 0), at 64 the law bit for bit with g = ${g}, LIVE the input bit for bit; the move from LOOP to 64 takes ${moved} frames, click ${moveClick.toFixed(2)} x steady`,
      );
      expect(moveClick).toBeLessThan(1.5);
    });
  });

  test('a value that is not finite or out of range is LIVE or LOOP, never a fault', async () => {
    await withRig({}, async (rig) => {
      await armedChange(rig, 3, 1);
      rig.pokeParam(0, 0x7fc00000);
      await rig.run(blocks(STEP), SOUND_B);
      const nan = await rig.run(4, SOUND_B);
      expect(sameBits(nan.left, nan.dryLeft), 'not a number: LIVE').toBe(0);
      rig.pokeParam(0, f2w(7));
      await rig.run(blocks(STEP), SOUND_B);
      expect((await rig.status()).gain, 'above 1: LOOP').toBe(1);
      rig.pokeParam(0, f2w(-3));
      await rig.run(blocks(STEP), SOUND_B);
      expect((await rig.status()).gain).toBe(0);
    });
  });
});

describe('Overloop engine: the loop', () => {
  test('a second armed change while a loop plays keeps what is heard then', async () => {
    await withRig({}, async (rig) => {
      const { change } = await armedChange(rig, 3, 1);
      // half way: B live and loop A, both with A's cycle, so what is heard repeats
      rig.setXfad(64);
      const input = switchAt(rig.clock + 3 * CYCLE, SOUND_B, SOUND_C);
      const mixed = await rig.run(blocks(2 * CYCLE + 3 * STEP), input);
      rig.arm();
      rig.changeAtEnd(C);
      const after = await rig.run(blocks(5 * STEP + 3 * CYCLE), input);
      const take = join(mixed, after);
      const second = 3 * CYCLE;
      const status = await rig.status();
      expect(status).toMatchObject({ loop: true, count: 2, loopFrames: CYCLE, armed: false });
      const worst = maxDifference(take.left, second, take.left, second - CYCLE, 3 * CYCLE);
      note(`second armed change with the crossfader at 64: 3 turns against the cycle before, largest difference ${worst}; click at the boundary ${click(take.left, second, second - CYCLE + 200, second - 200).toFixed(2)} x steady`);
      expect(worst).toBe(0);
      expect(click(take.left, second, second - CYCLE + 200, second - 200)).toBeLessThanOrEqual(1.001);
      // the new loop is the mix, not the first loop and not the input
      expect(maxDifference(take.left, second, take.dryLeft, second - CYCLE, CYCLE)).toBeGreaterThan(0.01);
      expect(change).toBe(3 * CYCLE);
      // the two buffers have swapped twice: the record runs in the first one again
      expect((await rig.state(OL.OL_ROLL!))[0]).toBe(0);
    });
  });

  test('a second armed change one cycle after the first: the whole cycle, and the frames before it for the seam', async () => {
    await withRig({}, async (rig) => {
      // The first loop plays alone for one cycle of B, and the change to C at B's end is armed. The record of that cycle
      // began with the first change, in the other buffer: the frames before it, which the seam runs into, come from the
      // first buffer.
      const { take: first, change } = await armedChange(rig, 3, 0);
      const second = change + CYCLE;
      const input = switchAt(second, SOUND_B, SOUND_C);
      const lead = await rig.run(blocks(STEP), input);
      rig.arm();
      rig.changeAtEnd(C);
      const rest = await rig.run(blocks(7 * STEP + 3 * CYCLE), input);
      const take = join(first, lead, rest);
      expect(await rig.status()).toMatchObject({ loop: true, count: 2, loopFrames: CYCLE, armed: false });
      const worst = maxDifference(take.left, second, take.left, second - CYCLE, 3 * CYCLE);
      const turn = click(take.left, second + CYCLE, second - CYCLE + 200, second - 200);
      note(`second armed change one cycle after the first: 3 turns against the cycle before, largest difference ${worst}; click at the loop's turn ${turn.toFixed(2)} x steady`);
      expect(worst).toBe(0);
      expect(maxDifference(take.right, second, take.right, second - CYCLE, 3 * CYCLE)).toBe(0);
      expect(turn).toBeLessThanOrEqual(1.001);
    });
  });

  test('a second armed change sooner than a cycle after the first keeps frames from before the first one too', async () => {
    await withRig({}, async (rig) => {
      // a direct change five steps after the first: the loop is the last eight steps heard, three of them from before
      const { take: first, change } = await armedChange(rig, 3, 0);
      const second = change + 5 * STEP;
      const input = switchAt(second, SOUND_B, SOUND_C);
      const lead = await rig.run(blocks(4 * STEP + STEP / 2), input);
      rig.arm();
      rig.changeNow(C);
      const rest = await rig.run(blocks(STEP / 2 + 3 * CYCLE), input);
      const take = join(first, lead, rest);
      expect(await rig.status()).toMatchObject({ loop: true, count: 2, loopFrames: CYCLE });
      const worst = maxDifference(take.left, second, take.left, second - CYCLE, 3 * CYCLE);
      note(`second armed change 5 of 8 steps after the first: 3 turns against one cycle earlier, largest difference ${worst}`);
      expect(worst).toBe(0);
    });
    await withRig({}, async (rig) => {
      // The limit. The frames from before the first change are filled in as fast as new ones are recorded: two steps
      // after it the record holds four steps, and the first four of the loop's eight are silence.
      const { take: first, change } = await armedChange(rig, 3, 0);
      const second = change + 2 * STEP;
      const input = switchAt(second, SOUND_B, SOUND_C);
      const lead = await rig.run(blocks(STEP + STEP / 2), input);
      rig.arm();
      rig.changeNow(C);
      const rest = await rig.run(blocks(STEP / 2 + 2 * CYCLE), input);
      const take = join(first, lead, rest);
      expect(await rig.status()).toMatchObject({ loop: true, count: 2, loopFrames: CYCLE });
      expect(peak(take.left, second, second + 4 * STEP), 'the part that was not filled in yet').toBe(0);
      expect(maxDifference(take.left, second + 4 * STEP, take.left, second - 4 * STEP, 4 * STEP - OL.OL_SEAM!)).toBe(0);
      note('second armed change 2 of 8 steps after the first: the first 4 steps of the loop are silence, the last 4 are the 4 steps heard before it');
    });
  });

  test('a cycle longer than the buffer is cut to its last part, and the status says so before and after', async () => {
    await withRig({ frames: 4096 }, async (rig) => {
      const long: Pattern = { id: A.id, steps: 16, stepFrames: STEP };
      const cycle = 16 * STEP;
      const sound = periodic(cycle, 23, 9, 0.3);
      rig.start(long);
      await rig.run(blocks(cycle + 3 * STEP), sound);
      const before = await rig.status();
      expect(before).toMatchObject({ track: true, nextFrames: cycle, nextCut: true, capacity: 4096 - OL.OL_SEAM! - BLOCK });
      rig.arm();
      rig.changeAtEnd(B);
      const first = await rig.run(blocks(13 * STEP), sound);
      const turns = await rig.run(blocks(3 * before.capacity + BLOCK * 3), SOUND_B);
      const take = join(first, turns);
      const change = 13 * STEP;
      const status = await rig.status();
      expect(status).toMatchObject({ loop: true, cut: true, loopFrames: before.capacity, loopSteps: 16 });
      // the loop is the last `capacity` frames before the change (up to its seam), and every turn is the first one again
      const body = status.loopFrames - OL.OL_SEAM!;
      const worst = maxDifference(take.left, change, take.left, change - status.loopFrames, body);
      const again = maxDifference(take.left, change + status.loopFrames, take.left, change, 2 * status.loopFrames);
      note(
        `a cycle of ${cycle} frames in buffers of 4096: the loop has the last ${status.loopFrames}; first turn against those frames, largest difference ${worst}; the next two turns against the first ${again}`,
      );
      expect(worst).toBe(0);
      expect(again).toBe(0);
      // the turn runs through the seam into the frames before the loop's start: no step there
      const turnAt = change + status.loopFrames;
      const seam = maxSecondDifference(take.left, turnAt - OL.OL_SEAM! - 64, turnAt + 64).value / maxSecondDifference(take.left, change - 2000, change - 200).value;
      note(`the cut loop's turn (its end runs into the frames before its start, which are another part of the cycle): click ${seam.toFixed(2)} x steady`);
      expect(seam).toBeLessThan(1.3);
    });
  });

  test('DROP: the crossfader runs to LIVE and the loop is gone; another DROP does nothing', async () => {
    await withRig({}, async (rig) => {
      const { take } = await armedChange(rig, 3, 1);
      rig.drop();
      const fade = await rig.run(blocks(STEP), SOUND_B);
      const status = await rig.status();
      expect(status).toMatchObject({ loop: false, loopFrames: 0, gain: 0, cut: false });
      const steady = await rig.run(blocks(CYCLE), SOUND_B);
      expect(sameBits(steady.left, steady.dryLeft)).toBe(0);
      // the fade runs from the loop (A) to the live sound (B): measured against the one of the two with the larger measure
      const reference = Math.max(
        maxSecondDifference(steady.left, 200, CYCLE - 200).value,
        maxSecondDifference(take.left, 200, CYCLE - 200).value,
      );
      const dropClick = maxSecondDifference(fade.left, 0, 256 + 64).value / reference;
      note(`drop at LOOP: LIVE after 256 frames, click ${dropClick.toFixed(2)} x steady (the louder of loop and live)`);
      // the crossfader moves evenly, 1/256 a frame, as it always did: its two corners between unlike sounds are this measure
      expect(dropClick).toBeLessThan(2.5);
      rig.drop();
      await rig.run(4, SOUND_B);
      expect((await rig.status()).loop).toBe(false);
      // the crossfader of the home stays where it was and means nothing without a loop
      expect(rig.xfad).toBe(127);
    });
  });
});

describe('Overloop engine: the boundary cases', () => {
  test('a direct change in the middle of a pattern keeps the last L frames, L the last whole cycle', async () => {
    await withRig({}, async (rig) => {
      rig.start(A);
      const lead = await rig.run(blocks(2 * CYCLE + 2 * STEP + STEP / 2), SOUND_A);
      rig.arm();
      rig.changeNow(B);
      const change = 2 * CYCLE + 3 * STEP;
      const input = switchAt(change, SOUND_A, SOUND_B);
      const rest = await rig.run(blocks(STEP / 2 + 3 * CYCLE), input);
      const take = join(lead, rest);
      const status = await rig.status();
      expect(status).toMatchObject({ loop: true, loopFrames: CYCLE, count: 1 });
      const worst = maxDifference(take.left, change, take.left, change - CYCLE, 3 * CYCLE);
      note(`direct change at step 4 of 8: loop ${status.loopFrames} frames; 3 turns against one cycle earlier, largest difference ${worst}; click ${click(take.left, change, change - CYCLE + 200, change - 200).toFixed(2)} x steady`);
      expect(worst).toBe(0);
      expect(click(take.left, change, change - CYCLE + 200, change - 200)).toBeLessThanOrEqual(1.001);
    });
  });

  test('a pattern that has played less than one cycle: the steps so far give the length, and the loop holds what was heard before START too', async () => {
    await withRig({}, async (rig) => {
      // stopped, with sound (the record runs all the time), then START and a direct change after three steps
      const lead = await rig.run(blocks(2 * CYCLE), SOUND_A);
      rig.start(A);
      const some = await rig.run(blocks(2 * STEP + STEP / 2), SOUND_A);
      rig.arm();
      rig.changeNow(B);
      const change = 2 * CYCLE + 3 * STEP;
      const rest = await rig.run(blocks(STEP / 2 + 2 * CYCLE), switchAt(change, SOUND_A, SOUND_B));
      const take = join(lead, some, rest);
      const status = await rig.status();
      expect(status).toMatchObject({ loop: true, loopFrames: CYCLE, count: 1 });
      expect(maxDifference(take.left, change, take.left, change - CYCLE, 2 * CYCLE)).toBe(0);
      note(`change after 3 of 8 steps since START: loop ${status.loopFrames} frames (3 steps of 320 frames give 8 x 320)`);
    });
    await withRig({}, async (rig) => {
      // nothing was recorded before: the part of the loop before the first recorded frame is silence, not old memory
      rig.start(A);
      const some = await rig.run(blocks(2 * STEP + STEP / 2), SOUND_A);
      rig.arm();
      rig.changeNow(B);
      const change = 3 * STEP;
      const rest = await rig.run(blocks(STEP / 2 + 2 * CYCLE), switchAt(change, SOUND_A, SOUND_B));
      const take = join(some, rest);
      const status = await rig.status();
      expect(status.loopFrames).toBe(CYCLE);
      const silent = CYCLE - 3 * STEP;
      expect(peak(take.left, change, change + silent), 'the frames that were never recorded').toBe(0);
      expect(maxDifference(take.left, change + silent, take.left, 0, 3 * STEP - OL.OL_SEAM!)).toBe(0);
      // the second turn is the first one again
      expect(maxDifference(take.left, change + CYCLE, take.left, change, CYCLE)).toBe(0);
    });
  });

  test('a change while the transport is stopped takes no loop and keeps the arm for the next change', async () => {
    await withRig({}, async (rig) => {
      rig.start(A);
      await rig.run(blocks(2 * CYCLE), SOUND_A);
      rig.stop();
      rig.arm();
      rig.select(B);
      const stopped = await rig.run(blocks(CYCLE), SOUND_A);
      expect(await rig.status()).toMatchObject({ armed: true, loop: false, track: false, steps: 0 });
      rig.start(B);
      const playing = await rig.run(blocks(2 * CYCLE + 3 * STEP), SOUND_B);
      expect(await rig.status()).toMatchObject({ armed: true, loop: false, count: 0 });
      expect(sameBits(stopped.left, stopped.dryLeft) + sameBits(playing.left, playing.dryLeft)).toBe(0);
      // the next change while it plays is the armed one
      rig.changeAtEnd(C);
      const change = rig.clock + 5 * STEP;
      const after = await rig.run(blocks(5 * STEP + CYCLE), switchAt(change, SOUND_B, SOUND_C));
      expect(await rig.status()).toMatchObject({ armed: false, loop: true, count: 1, loopFrames: CYCLE });
      const both = join(playing, after);
      const at = playing.left.length + 5 * STEP;
      expect(maxDifference(both.left, at, both.left, at - CYCLE, CYCLE)).toBe(0);
    });
  });

  test('STOP leaves the loop playing; START begins it again with the pattern, without a click', async () => {
    await withRig({}, async (rig) => {
      const { take, change } = await armedChange(rig, 3, 1);
      const origin = change - CYCLE;
      rig.stop();
      const stopped = await rig.run(blocks(CYCLE + 5 * STEP), SOUND_B);
      let worst = 0;
      for (let k = 0; k < stopped.left.length; k++)
        worst = Math.max(worst, Math.abs(stopped.left[k]! - take.left[origin + ((stopped.start + k - change) % CYCLE)]!));
      expect(worst, 'stopped: the loop goes on in its phase').toBe(0);
      // START in the middle of the loop: it begins again with the first step
      rig.start(B);
      const started = await rig.run(blocks(2 * CYCLE), SOUND_B);
      const settle = OL.OL_JUMP_FRAMES!;
      worst = 0;
      for (let k = settle; k < started.left.length; k++) worst = Math.max(worst, Math.abs(started.left[k]! - take.left[origin + (k % CYCLE)]!));
      const all = join(stopped, started);
      const at = stopped.left.length;
      const measure = maxSecondDifference(all.left, at - 8, at + settle + 8).value / maxSecondDifference(take.left, origin + 200, change - 200).value;
      note(`START at phase ${(stopped.start + stopped.left.length - change) % CYCLE} of the loop: from ${settle} frames on it is the loop from its start (difference ${worst}); click over the restart ${measure.toFixed(2)} x steady`);
      expect(worst).toBe(0);
      expect(measure).toBeLessThan(1.6);
      expect(await rig.status()).toMatchObject({ loop: true, count: 1, loopFrames: CYCLE });
    });
  });

  test('a lease that changes under the running engine: it starts anew in the new buffers, without a fault', async () => {
    await withRig({}, async (rig) => {
      await armedChange(rig, 3, 1);
      expect((await rig.status()).loop).toBe(true);
      // the second buffer's descriptor now says half the frames
      rig.pokePhraseLease(5, 8192);
      const moved = await rig.run(4, SOUND_B);
      expect(sameBits(moved.left, moved.dryLeft) + sameBits(moved.right, moved.dryRight), 'the input plays').toBe(0);
      // the new engine goes on counting loops from its status: a loop it takes has a count no reader has seen
      expect(await rig.status()).toMatchObject({ loop: false, armed: false, count: 1, gain: 0, capacity: 8192 - OL.OL_SEAM! - BLOCK });
      // and an armed change works as before
      rig.stop();
      const { take, change } = await armedChange(rig, 2, 1);
      expect(await rig.status()).toMatchObject({ loop: true, count: 2, loopFrames: CYCLE });
      expect(maxDifference(take.left, change, take.left, change - CYCLE, CYCLE)).toBe(0);
    });
  });

  test('START while the crossfader moves, and the seam while it stands between: the heaviest blocks', async () => {
    await withRig({ tap: true }, async (rig) => {
      const { take, change } = await armedChange(rig, 3, 1);
      rig.stop();
      await rig.run(blocks(3 * STEP), SOUND_B);
      // the restart's declick, the crossfader on its way and the mix, all in the same blocks
      rig.setXfad(30);
      rig.start(B);
      const moving = await rig.run(blocks(STEP), SOUND_B);
      // then the seam with the crossfader between LIVE and LOOP
      const turn = await rig.run(blocks(CYCLE), SOUND_B);
      const g = Math.fround(30 / 127);
      const inv = w2f((await rig.state(OL.OL_INV!))[0]!);
      let worst = 0;
      for (let k = 0; k < turn.left.length; k++) {
        const loop = take.left[change - CYCLE + ((STEP + k) % CYCLE)]!;
        // the tap is the loop times the crossfader, then divided by the master's gain, in single precision
        worst = Math.max(worst, Math.abs(turn.tapLeft[k]! - Math.fround(Math.fround(loop * g) * inv)));
      }
      expect(worst, 'the tap at a crossfader of 30').toBe(0);
      note(
        `heaviest blocks: the restart with the crossfader moving ${Math.max(...moving.instructions)} instructions, the seam at a crossfader of 30 ${Math.max(...turn.instructions)}`,
      );
    });
  });

  test('a tempo change after the capture leaves the loop its recorded length', async () => {
    await withRig({}, async (rig) => {
      const { take, change } = await armedChange(rig, 3, 1);
      rig.setStepFrames(STEP + 96);
      const slower = await rig.run(blocks(3 * CYCLE), SOUND_B);
      let worst = 0;
      for (let k = 0; k < slower.left.length; k++)
        worst = Math.max(worst, Math.abs(slower.left[k]! - take.left[change - CYCLE + ((slower.start + k - change) % CYCLE)]!));
      const status = await rig.status();
      expect(worst).toBe(0);
      expect(status).toMatchObject({ loopFrames: CYCLE, track: true, nextFrames: 8 * (STEP + 96) });
      note(`steps of ${STEP + 96} frames after a loop of ${CYCLE}: the loop still turns every ${CYCLE} frames; an armed change would now keep ${status.nextFrames}`);
    });
  });

  test('a position stream that breaks takes no loop and costs no sound', async () => {
    await withRig({}, async (rig) => {
      rig.start(A);
      await rig.run(blocks(2 * CYCLE), SOUND_A);
      rig.arm();
      // a record that is not one, then a jump of the send count: the run is forgotten and begins again with the next boundary
      rig.patchPosition(0, 0);
      await rig.run(1, SOUND_A);
      expect(await rig.status()).toMatchObject({ track: false, armed: true });
      rig.patchPosition(2, 0x7000);
      const broken = await rig.run(blocks(3 * STEP), SOUND_A);
      expect(sameBits(broken.left, broken.dryLeft)).toBe(0);
      expect((await rig.status()).loop).toBe(false);
    });
  });
});

describe('Overloop engine: a position stream whose records come again or late (the device link, docs/findings/363)', () => {
  // With armedChange(rig, 4, ...) the last cycle of A runs from block LAST to the change at block CHANGE; ARM and the change are
  // set at block 270. Steps begin every 10 blocks.
  const CHANGE = blocks(4 * CYCLE);
  const LAST = CHANGE - blocks(CYCLE);

  /** A delivery that keeps what it gave: the record of each block. */
  const recorded = (delivery: (block: number) => number) => {
    const given: number[] = [];
    return {
      given,
      delivery: (block: number) => {
        given[block] = delivery(block);
        return given[block]!;
      },
    };
  };

  test('a record again every 37 blocks, one a block early every 53 and a burst of three: an armed change keeps the last cycle exactly', async () => {
    const late = (b: number) => b % 37 === 22 || (b >= 300 && b < 303);
    const early = (b: number) => b % 53 === 47;
    const { delivery, given } = recorded(jitter(late, early));
    await withRig({ delivery }, async (rig) => {
      const { take, change } = await armedChange(rig, 4, 5);
      const status = await rig.status();
      expect(status).toMatchObject({ loop: true, armed: false, cut: false, count: 1, loopFrames: CYCLE, loopSteps: 8 });
      const turns = 5 * CYCLE;
      expect(maxDifference(take.left, change, take.left, change - CYCLE, turns), 'five turns against one cycle earlier').toBe(0);
      expect(maxDifference(take.right, change, take.right, change - CYCLE, turns)).toBe(0);
      // what the jitter did: records seen again, gaps, and the steps it moved (the block a boundary was seen in, against its own)
      const again = given.filter((n, b) => b > 0 && n === given[b - 1]).length;
      const gaps = given.flatMap((n, b) => (b > 0 && n - given[b - 1]! > 1 ? [n - given[b - 1]!] : []));
      const moved = rig.log
        .map(([frame]) => [frame / BLOCK, given.findIndex((n) => n >= frame / BLOCK)] as const)
        .filter(([own, seen]) => own !== seen);
      note(
        `jitter over ${given.length} blocks: ${again} records seen again, ${gaps.length} gaps (largest ${Math.max(...gaps)}), steps seen late or early ${moved.map(([own, seen]) => `${own}->${seen}`).join(' ')}; loop ${status.loopFrames} frames, exact`,
      );
      expect(again).toBeGreaterThanOrEqual(30);
      expect(Math.max(...gaps), 'the burst of three: a gap of four').toBe(OL.OL_SKIP);
      expect(moved.length, 'the jitter moved steps of A and B').toBeGreaterThanOrEqual(2);
      expect(moved.some(([own]) => own === LAST || own === CHANGE), 'but neither end of the last cycle').toBe(false);
    });
  });

  test.each([
    ["the last cycle's first step is seen a block late", (b: number) => b === LAST, () => false, -1],
    ['the change is seen a block late', (b: number) => b === CHANGE, () => false, 1],
    ['the change is seen a block early', () => false, (b: number) => b === CHANGE - 1, -1],
  ])('%s: the loop is one block off', async (what, late, early, off) => {
    await withRig({ delivery: jitter(late, early) }, async (rig) => {
      await armedChange(rig, 4, 1);
      const status = await rig.status();
      note(`${what}: loop ${status.loopFrames} frames for a cycle of ${CYCLE}`);
      expect(status).toMatchObject({ loop: true, armed: false, count: 1, loopSteps: 8 });
      expect(status.loopFrames).toBe(CYCLE + off * BLOCK);
    });
  });

  /** Pattern A, ARM and a change at the end of the fourth cycle; `fault` is applied to the record of the change's block. */
  async function faultAtChange(rig: LoopRig, fault: (rig: LoopRig) => unknown): Promise<void> {
    rig.start(A);
    await rig.run(blocks(3 * CYCLE + 3 * STEP), SOUND_A);
    rig.arm();
    rig.changeAtEnd(B);
    await rig.run(blocks(5 * STEP), SOUND_A);
    expect(rig.clock / BLOCK).toBe(CHANGE);
    await fault(rig);
    await rig.run(1, SOUND_B);
  }
  const none = () => undefined;
  test.each([
    ['a send count that moved by OL_SKIP + 1', (rig: LoopRig) => rig.skipFrames(OL.OL_SKIP!), undefined, false],
    ['a send count that moved by OL_SKIP', (rig: LoopRig) => rig.skipFrames(OL.OL_SKIP! - 1), undefined, true],
    ['a send count that went back', async (rig: LoopRig) => rig.patchPosition(2, (await rig.positionWord(2)) - 1), undefined, false],
    ['two boundaries in one record', async (rig: LoopRig) => rig.patchPosition(3, (await rig.positionWord(3)) + 2), undefined, false],
    ['OL_SKIP records again in a row before it', none, jitter((b) => b >= CHANGE - 4 && b < CHANGE), false],
    ['OL_SKIP - 1 records again in a row before it', none, jitter((b) => b >= CHANGE - 3 && b < CHANGE), true],
  ])('%s, at the armed change: the change keeps a loop: %s', async (_, fault, delivery, kept) => {
    await withRig({ delivery }, async (rig) => {
      await faultAtChange(rig, fault);
      if (kept) {
        expect(await rig.status()).toMatchObject({ loop: true, armed: false, count: 1, loopFrames: CYCLE });
        return;
      }
      // the run ended: no edge is placed with this record, and the change takes nothing; the arm waits for the next change
      expect(await rig.status()).toMatchObject({ loop: false, armed: true, count: 0, track: false });
      await rig.run(blocks(2 * CYCLE), SOUND_B);
      expect(await rig.status(), 'the run follows B again').toMatchObject({ loop: false, armed: true, count: 0, track: true, steps: 8 });
    });
  });
});

describe("Overloop engine: a send effect's output (the wet mailbox)", () => {
  /** A send effect's output as PRISM would give it: periodic with the cycle, so it is easy to tell from the patterns. */
  const WET: Signal = (n) => {
    const p = (2 * Math.PI * (n % CYCLE)) / CYCLE;
    return [0.2 * Math.sin(13 * p), 0.15 * Math.sin(19 * p)];
  };
  const BOOST = w2f(OL.OL_MASTER_BOOST!);
  test("behind the crossfader at every position, at the master's gain, and never in the record", async () => {
    const run = async (wet: boolean) =>
      withRig({}, async (rig) => {
        rig.setMaster(0.8, 0);
        if (wet) rig.wet = { signal: WET };
        const { take: first } = await armedChange(rig, 3, 1);
        rig.setXfad(64);
        const half = await rig.run(blocks(CYCLE), SOUND_B);
        rig.setXfad(0);
        const live = await rig.run(blocks(CYCLE), SOUND_B);
        return { take: join(first, half, live), buffers: [await rig.buffer(0), await rig.buffer(1)], inv: w2f((await rig.state(OL.OL_INV!))[0]!) };
      });
    const a = await run(true);
    const b = await run(false);
    const change = 3 * CYCLE;
    const n = a.take.left.length;
    // from the second block on (the engine's first block only notes the mailbox's count): before the loop (LIVE), the loop
    // at LOOP, the crossfader at 64, at LIVE; both sides
    let differ = 0;
    for (const [out, base, wet] of [
      [a.take.left, b.take.left, a.take.wetLeft],
      [a.take.right, b.take.right, a.take.wetRight],
    ] as const) {
      for (let at = BLOCK; at < n; at++) {
        const term = Math.fround(Math.fround(wet[at]! * BOOST) * Math.fround(0.8));
        if (f2w(out[at]!) !== f2w(Math.fround(base[at]! + term))) differ += 1;
      }
    }
    expect(differ, 'every sample is the output without the send effect plus its output times the +10 dB stage and the ramp').toBe(0);
    expect(sameBits(a.take.left, b.take.left, 0, BLOCK), 'the first block notes the count only').toBe(0);
    // the record and the loop: the same words with and without the send effect
    for (const k of [0, 1]) expect(a.buffers[k]!.every((w, i) => w === b.buffers[k]![i]), `buffer ${k}`).toBe(true);
    // the tap is divided by the master's gain at the block's end
    expect(Math.abs(a.inv * BOOST * 0.8 - 1)).toBeLessThan(1e-6);
    const level = rms(a.take.left, change, change + CYCLE) / rms(b.take.left, change, change + CYCLE);
    note(`send effect's output added at LIVE, LOOP, 64 and LIVE again, ramp at 0.8: every sample as the master's gain gives it (differences 0); the two buffers with and without it are the same words; output level with it ${level.toFixed(3)} x without`);
  });

  test('the volume ramp sample by sample, the bound, and the dry path untouched when the send effect is silent', async () => {
    await withRig({}, async (rig) => {
      // a ramp from 0.5 down by 1/4096 a sample, as the stock code adds it up
      const start = 0.5;
      const step = -1 / 4096;
      rig.setMaster(start, step);
      rig.wet = { signal: WET };
      rig.start(A);
      const take = await rig.run(40, SOUND_A);
      let differ = 0;
      for (let b = BLOCK; b < take.left.length; b += BLOCK) {
        let g = Math.fround(start);
        for (let k = 0; k < BLOCK; k++) {
          const term = Math.fround(Math.fround(take.wetLeft[b + k]! * BOOST) * g);
          if (f2w(take.left[b + k]!) !== f2w(Math.fround(take.dryLeft[b + k]! + term))) differ += 1;
          g = Math.fround(Math.fround(step) + g);
        }
      }
      expect(differ, 'the gain before sample k is start + k steps').toBe(0);
      // a silent send effect: the output is the input, bit for bit
      rig.wet = { signal: () => 0 };
      const silent = await rig.run(20, SOUND_A);
      expect(sameBits(silent.left, silent.dryLeft) + sameBits(silent.right, silent.dryRight)).toBe(0);
      // a count that does not move: nothing is added
      rig.wet = { signal: WET, stale: true };
      const stale = await rig.run(20, SOUND_A);
      expect(sameBits(stale.left, stale.dryLeft)).toBe(0);
      // a block that is not finite, or louder than 8: not added, and no fault
      rig.wet = { signal: (n) => (n % BLOCK === 5 ? Number.NaN : 0.1) };
      const bad = await rig.run(4, SOUND_A);
      expect(sameBits(bad.left, bad.dryLeft)).toBe(0);
      rig.wet = { signal: () => 9 };
      const loud = await rig.run(4, SOUND_A);
      expect(sameBits(loud.left, loud.dryLeft)).toBe(0);
      // a loud one within the bound: the output stays within 7.9
      rig.setMaster(1, 0);
      rig.wet = { signal: () => 5 };
      const top = await rig.run(4, SOUND_A);
      expect(peak(top.left, BLOCK)).toBeCloseTo(7.9, 5);
      // a wrong magic: nothing
      rig.wet = { signal: WET, magic: 0 };
      const wrong = await rig.run(4, SOUND_A);
      expect(sameBits(wrong.left, wrong.dryLeft)).toBe(0);
      note('volume ramp from 0.5 by -1/4096 a sample: every sample as the stock ramp adds it up; silent, stale, not finite, louder than 8 and wrong magic: the input bit for bit; within the bound the output stays at 7.9');
    });
  });

  test("its count moves once a block the engine finishes, never in a block that faulted", async () => {
    await withRig({ faults: true }, async (rig) => {
      rig.start(A);
      await rig.run(10, SOUND_A);
      expect(await rig.taken()).toBe(10);
      rig.pokeLoopLease(0, 0);
      const broken = await rig.run(3, SOUND_A);
      expect(broken.faulted).toBe(3);
      expect(await rig.taken()).toBe(10);
    });
  });
});

describe('Overloop engine: the buffers', () => {
  test('buffers as the device leases them: a cycle of 96000 frames at steps that are no whole number of blocks', async () => {
    await withRig({ frames: DEVICE_FRAMES, poison: false }, async (rig) => {
      // 16 steps of 6000 frames (120 BPM) are 187.5 blocks each: the steps alternate between 187 and 188 blocks
      const real: Pattern = { id: A.id, steps: 16, stepFrames: [5984, 6016] };
      const cycle = 96000;
      const sound = periodic(cycle, 440, 1310, 0.3);
      rig.start(real);
      const lead = await rig.run(blocks(cycle + 8 * 6000), sound);
      rig.arm();
      rig.changeAtEnd(B);
      const rest = await rig.run(blocks(8 * 6000 + cycle + 960), switchAt(2 * cycle, sound, SOUND_B));
      const take = join(lead, rest);
      const change = 2 * cycle;
      const status = await rig.status();
      expect(status).toMatchObject({ loop: true, cut: false, loopFrames: cycle, loopSteps: 16, capacity: DEVICE_FRAMES - OL.OL_SEAM! - BLOCK });
      const worst = maxDifference(take.left, change, take.left, change - cycle, cycle + 960);
      note(`device buffers (${DEVICE_FRAMES} frames, ${(DEVICE_FRAMES / FS).toFixed(2)} s; longest loop ${status.capacity} frames): loop of ${status.loopFrames} frames, one turn and 20 ms against one cycle earlier, largest difference ${worst}`);
      expect(worst).toBe(0);
    });
  });

  test('descriptors that are not valid: the adapter counts a fault and the input plays', async () => {
    for (const breakIt of [
      (rig: LoopRig) => rig.pokeLoopLease(0, 0),
      (rig: LoopRig) => rig.pokePhraseLease(1, 7),
      (rig: LoopRig) => rig.pokeLoopLease(2, 0x20000000),
      (rig: LoopRig) => rig.pokePhraseLease(5, 1024),
      // the two buffers overlap
      (rig: LoopRig) => rig.pokePhraseLease(3, 0x9aa2b9c8 + 4096),
    ]) {
      await withRig({ faults: true }, async (rig) => {
        breakIt(rig);
        const take = await rig.run(3, SOUND_A);
        expect(take.faulted).toBeGreaterThan(0);
      });
    }
  });

  test('under a hostile context the module stores only to its state, its output, the two buffers, the status and the tap', async () => {
    await withRig({ stores: true }, async (rig) => {
      await armedChange(rig, 2, 1);
      rig.setXfad(40);
      await rig.run(blocks(CYCLE), SOUND_B);
      rig.stop();
      rig.start(B);
      await rig.run(blocks(CYCLE), SOUND_B);
      rig.drop();
      await rig.run(blocks(STEP), SOUND_B);
      expect(rig.foreign).toEqual([]);
      // the buffers: a NaN word is a frame the engine never wrote, and none of them reached the output (the rig checks every block)
      const first = await rig.buffer(0);
      expect(first.some((w) => w !== 0x7fc00001), 'the rolling record wrote the first buffer').toBe(true);
    });
  });
});

describe('Overloop engine: cost', () => {
  test('the heaviest block of every case here is the cost module.toml declares', async () => {
    const idle = await withRig({}, async (rig) => {
      rig.start(A);
      const take = await rig.run(blocks(CYCLE), SOUND_A);
      return Math.max(...take.instructions);
    });
    note(`a block without a loop: at most ${idle} instructions; declared dsp_cost_fx ${DECLARED}`);
    expect(LoopRig.worst).toBeLessThanOrEqual(DECLARED);
    expect(LoopRig.worst, 'the declared cost is the measured one').toBe(DECLARED);
  });
});
