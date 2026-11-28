/**
 * PRISM's rest on the SHARC-only rig (docs/findings/377): when the engine stops running its block, when it does not, what
 * wakes it, and that waking is not heard. The reference of a case is the same engine kept awake (the rig clears its count
 * of silent frames before every block): it plays every block in full, as the engine did before rest. Numbers go to
 * out/runs/prism-engine/rest.txt.
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import { afterAll, describe, expect, test } from 'vitest';
import {
  BLOCK,
  LOOP,
  CH,
  PRISM,
  PS,
  PrismRig,
  burst,
  maxSecondDifference,
  peak,
  rms,
  silence,
  sine,
  sum,
  w2f,
  zoneTime,
  type Controls,
  type PrismRigOptions,
  type Signal,
  type Take,
} from './engine-rig.ts';
import { PairRig } from './pair-rig.ts';

const HZ = 220;
const LEVEL = w2f(PRISM.PRISM_IDLE_LEVEL!);
const QUIET = PRISM.PRISM_IDLE_QUIET!;
const LATE = PRISM.PRISM_IDLE_LATE!;
/** A resting block (core/fx and the engine) costs less than REST_MOST instructions; a block that runs more than RUN_LEAST (the lightest, with the tape standing, is about 3100). */
const REST_MOST = 1600;
const RUN_LEAST = 2500;

const measures: string[] = [];
const note = (line: string): void => void measures.push(line);
afterAll(() => {
  const report = ['PRISM rest:', ...measures].join('\n  ');
  console.info(report);
  mkdirSync('out/runs/prism-engine', { recursive: true });
  writeFileSync('out/runs/prism-engine/rest.txt', `${report}\n`);
});

async function withRig<T>(o: PrismRigOptions, body: (rig: PrismRig) => Promise<T>): Promise<T> {
  const rig = await PrismRig.open(o);
  try {
    return await body(rig);
  } finally {
    await rig.close();
  }
}

const word = async (rig: PrismRig, at: number): Promise<number> => (await rig.state(at))[0]!;
/** The engine rests from the next block on (PS_IDLE 1, set by the last block that ran) or rests already (2). */
const resting = async (rig: PrismRig): Promise<boolean> => (await word(rig, PS.IDLE)) !== 0;

/** Run `input` block by block until a block lets the engine rest: the blocks that ran in full, and their takes. */
async function untilRest(rig: PrismRig, most: number, input: Signal = silence): Promise<{ blocks: number; takes: Take[] }> {
  const takes: Take[] = [];
  for (let b = 1; b <= most; b++) {
    takes.push(await rig.run(1, input));
    if (await resting(rig)) return { blocks: b, takes };
  }
  throw new Error(`the engine did not rest in ${most} blocks`);
}

/** Blocks of `input` that ran in full, counted until the first that rested (at most `most`). */
async function ranBefore(rig: PrismRig, most: number, input: Signal = silence): Promise<number> {
  for (let b = 0; b < most; b++) {
    const take = await rig.run(1, input);
    if (take.instructions[0]! < REST_MOST) return b;
  }
  return most;
}

/** The last sample of the takes at or above the rest level, in frames from the first take's start (-1: none). */
function lastSound(takes: Take[]): number {
  let last = -1;
  let at = 0;
  for (const t of takes) {
    for (let n = 0; n < t.left.length; n++) if (Math.abs(t.left[n]!) >= LEVEL || Math.abs(t.right[n]!) >= LEVEL) last = at + n;
    at += t.left.length;
  }
  return last;
}

/**
 * The same run on the engine as it is, then on one kept awake. `settle` is the silence of a run: on the engine as it is, until
 * it rests and `more` blocks on; on the one kept awake, as many blocks.
 */
let settled: number[] = [];
let settledAt = 0;
async function settle(rig: PrismRig, awake: boolean, most: number, more = 40, input: Signal = silence): Promise<Take[]> {
  if (awake) return [await rig.run(settled[settledAt++]!, input)];
  const { blocks, takes } = await untilRest(rig, most, input);
  takes.push(await rig.run(more, input));
  settled.push(blocks + more);
  return takes;
}
async function both(o: PrismRigOptions, body: (rig: PrismRig, awake: boolean) => Promise<Take[]>): Promise<[Take[], Take[]]> {
  settled = [];
  settledAt = 0;
  const a = await withRig(o, (rig) => body(rig, false));
  const b = await withRig({ ...o, awake: true }, (rig) => body(rig, true));
  return [a, b];
}
const largest = (x: Float32Array, y: Float32Array): number => {
  let worst = 0;
  for (let n = 0; n < x.length; n++) worst = Math.max(worst, Math.abs(x[n]! - y[n]!));
  return worst;
};

/** A synced echo of 2400 frames with a few repeats: the tape plays out in a few seconds. */
const ECHO: Partial<Controls> = { sync: true, syncTime: 2400, rpts: 0.3 };
const NOTE = burst(HZ, 0.3, 0, 1600);

describe('PRISM rest: when the engine rests', () => {
  test('after the tape has played out and one whole tape of silence was written, a block rests: it costs a fraction and the output is 0', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      rig.set(ECHO);
      const played = await rig.run(60, NOTE);
      const full = Math.max(...played.instructions);
      const { blocks, takes } = await untilRest(rig, 6000);
      const sound = lastSound(takes);
      const valid = await word(rig, PS.VALID);
      expect(valid).toBe(16384);
      // Not before the whole tape was written with silence and the wet signal was silent for PRISM_IDLE_QUIET frames after
      // that. The feedback is the wet signal times 0.4, so the last frame written above the level lies up to 2.5 turns
      // before the last such frame of the output.
      expect(blocks * BLOCK).toBeGreaterThanOrEqual(sound + 1 + valid + QUIET - 3 * 2400);
      expect(blocks * BLOCK).toBeGreaterThanOrEqual(sound + 1 + QUIET);
      expect(blocks * BLOCK).toBeLessThanOrEqual(sound + 1 + valid + QUIET + 2 * BLOCK);
      expect(await word(rig, PS.TAPE_QUIET)).toBeGreaterThanOrEqual(valid);
      expect(await word(rig, PS.QUIET)).toBeGreaterThanOrEqual(QUIET);
      // the block that set the state ran in full; the first resting block empties the filters; the ones after it are the rest
      const first = (await rig.run(1)).instructions[0]!;
      const rest = await rig.run(200);
      expect(Math.max(...rest.instructions)).toBeLessThan(REST_MOST);
      expect(first).toBeLessThan(REST_MOST);
      expect(peak(rest.left)).toBe(0);
      expect(peak(rest.right)).toBe(0);
      expect(await resting(rig)).toBe(true);
      // the tape holds exact silence where the resting blocks wrote
      const wpos = await word(rig, PS.WPOS);
      const tape = await rig.tapeLeft(wpos >= 64 ? wpos - 64 : 16384 - 64, 64);
      expect(Array.from(tape).every((v) => Object.is(v, 0))).toBe(true);
      note(
        `rest after a note: the last sample at or above 2^-40 at frame ${sound}, resting from block ${blocks} (frame ${blocks * BLOCK}); a block that plays ${full} instructions, a resting block ${Math.max(...rest.instructions)} (the first after a block that ran: ${first})`,
      );
    });
  });

  test('with HALO up the late network gets PRISM_IDLE_LATE frames of silence first, and the first resting block empties lines and rings', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      rig.set({ ...ECHO, rpts: 0, halo: 0.8 });
      await rig.run(60, NOTE);
      const { blocks, takes } = await untilRest(rig, 12000);
      const sound = lastSound(takes);
      // the wet signal in front of the late network falls silent before the output does
      expect(blocks * BLOCK).toBeGreaterThanOrEqual(LATE);
      expect(blocks * BLOCK).toBeGreaterThanOrEqual(sound + 1);
      expect(await word(rig, PS.QUIET)).toBe(LATE);
      const first = (await rig.run(1)).instructions[0]!;
      const rest = await rig.run(50);
      expect(Math.max(...rest.instructions)).toBeLessThan(REST_MOST);
      // 1024 words of lines and 2048 of rings, once
      expect(first).toBeGreaterThan(3072);
      expect(first).toBeLessThan(3072 + REST_MOST + 100);
      const lines = await rig.state(384, 1024);
      const rings = await rig.state(1480, 2048);
      expect(lines.every((w) => w === 0)).toBe(true);
      expect(rings.every((w) => w === 0)).toBe(true);
      note(
        `rest with HALO 0.8, one echo: the output's last sample at or above 2^-40 at frame ${sound}, resting from frame ${blocks * BLOCK} (PRISM_IDLE_LATE ${LATE}); the first resting block ${first} instructions, then ${Math.max(...rest.instructions)}`,
      );
    });
  });

  test('an input under the rest level is silence; one at it is not', async () => {
    await withRig({ frames: 4096 }, async (rig) => {
      rig.set(ECHO);
      const under: Signal = () => LEVEL / 4;
      const { blocks } = await untilRest(rig, 400, under);
      // an empty tape: both sides play once 2400 + 70 frames are written, their crossfade ends 960 samples on, and the
      // tape is full of silence at 4096 frames
      expect(blocks).toBe(4096 / BLOCK);
      expect(Math.max(...(await rig.run(50, under)).instructions)).toBeLessThan(REST_MOST);
      // one sample at the level wakes the block
      const at: Signal = (n) => (n % BLOCK === 17 ? -LEVEL : 0);
      expect((await rig.run(1, at)).instructions[0]!).toBeGreaterThan(RUN_LEAST);
      // and one above it on the tape (here with the dust's first echo under it) counts as sound: a whole tape is written again
      expect((await rig.run(1, (n) => 2 * at(n))).instructions[0]!).toBeGreaterThan(RUN_LEAST);
      expect(await word(rig, PS.IDLE)).toBe(0);
      expect(await word(rig, PS.TAPE_QUIET)).toBe(0);
      // a whole tape of silence is written after it, then the wet signal is silent for PRISM_IDLE_QUIET frames
      expect(await ranBefore(rig, 400)).toBe((4096 + QUIET) / BLOCK);
    });
  });
});

describe('PRISM rest: when it does not', () => {
  test('HOLD on a tape that sounds never rests, and the loop goes on', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      rig.set({ ...ECHO, rpts: 0 });
      await rig.run(200, sine(HZ, 0.2));
      rig.set({ hold: true });
      await rig.run(100, sine(HZ, 0.2));
      let level = 1;
      for (let k = 0; k < 40; k++) {
        const take = await rig.run(100);
        expect(await word(rig, PS.IDLE)).toBe(0);
        expect(Math.min(...take.instructions)).toBeGreaterThan(RUN_LEAST);
        level = Math.min(level, rms(take.left));
      }
      expect(level).toBeGreaterThan(0.05);
      note(`HOLD on a sounding tape, 4000 blocks of silent input: never resting, the loop's lowest rms over 100 blocks ${level.toFixed(4)}`);
    });
  });

  test('repeats at unity with a sound on the tape never rest, and the sound goes on', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      rig.set({ ...ECHO, rpts: 0.75 });
      await rig.run(60, burst(HZ, 0.2, 0, 1600));
      const levels: number[] = [];
      for (let k = 0; k < 40; k++) {
        const take = await rig.run(150);
        expect(await word(rig, PS.IDLE)).toBe(0);
        expect(Math.min(...take.instructions)).toBeGreaterThan(RUN_LEAST);
        levels.push(peak(take.left));
      }
      expect(levels.at(-1)!).toBeGreaterThan(0.9 * levels[0]!);
      note(`unity repeats, 6000 blocks of silent input: never resting, the repeat's peak ${levels[0]!.toFixed(4)} then ${levels.at(-1)!.toFixed(4)}`);
    });
  });

  test('a crossfade of the heads, the frames after HOLD and the write crossfade after a release are finished first', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      rig.set(ECHO);
      await untilRest(rig, 1200);
      // (4800 + 70 frames of tape first: a side without tape for its time falls silent and comes back)
      await rig.run(200);
      // a SYNC step is a jump: the two heads crossfade for PRISM_XFADE samples
      rig.set({ syncTime: 4800 });
      const jump = await ranBefore(rig, 100);
      expect(jump).toBe(Math.ceil(PRISM.PRISM_XFADE! / BLOCK));
      expect(await resting(rig)).toBe(true);
      // HOLD on a silent tape: the tape takes PRISM_WFADE frames more, and the heads crossfade into the held loop; then
      // the engine rests with the tape standing
      await rig.run(20);
      const before = await word(rig, PS.WPOS);
      rig.set({ hold: true });
      const hold = await ranBefore(rig, 200);
      expect(hold).toBeGreaterThanOrEqual(PRISM.PRISM_WFADE! / BLOCK);
      expect(hold).toBeLessThanOrEqual((PRISM.PRISM_WFADE! + PRISM.PRISM_XFADE!) / BLOCK + 1);
      expect(await word(rig, PS.HOLD)).toBe(2);
      const stands = await word(rig, PS.WPOS);
      expect(stands).toBe((before + PRISM.PRISM_WFADE!) % 16384);
      await rig.run(50);
      expect(await resting(rig)).toBe(true);
      expect(await word(rig, PS.WPOS)).toBe(stands);
      expect((await rig.status())[3]).toBe(5);
      // the release crossfades the write into the tape, which does not count as silence: a whole tape is written again
      rig.set({ hold: false });
      const release = await ranBefore(rig, 2000);
      expect(release).toBeGreaterThanOrEqual((16384 + QUIET) / BLOCK);
      expect(release).toBeLessThanOrEqual((16384 + QUIET + PRISM.PRISM_WFADE!) / BLOCK + 2);
      note(`blocks that ran before the engine rested again: a SYNC step ${jump}, HOLD taken on a silent tape ${hold}, its release ${release}`);
    });
  });

  test('a side that is still filling keeps the engine running until it plays', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      // 9000 frames: the heads play once 9070 frames are written and their crossfade of 960 samples is done
      rig.set({ sync: true, syncTime: 9000 });
      const { blocks } = await untilRest(rig, 2000);
      // the time is half the tape at most: 8192 frames
      expect(blocks).toBe(Math.ceil((8192 + 70) / BLOCK) + PRISM.PRISM_XFADE! / BLOCK);
      expect((await rig.status())[3]).toBe(2);
    });
  });
});

describe('PRISM rest: waking', () => {
  // The same run on the engine and on one kept awake. A note, a long silence (the engine rests in it), then an impulse and a
  // note 3000 frames later.
  const wake = (controls: Partial<Controls>, most: number, frames = 4000) =>
    both({ frames: 16384 }, async (rig, awake) => {
      rig.set(controls);
      await rig.run(60, NOTE);
      const quiet = await settle(rig, awake, most, 300);
      if (!awake) expect(await resting(rig)).toBe(true);
      else expect(Math.min(...quiet[0]!.instructions)).toBeGreaterThan(RUN_LEAST);
      const from = rig.clock;
      const sound = sum((n) => (n === from + 7 ? 0.5 : 0), burst(HZ, 0.3, from + 3000, frames));
      const take = await rig.run(500, sound);
      if (!awake) expect(take.instructions[0]!).toBeGreaterThan(RUN_LEAST);
      return [take];
    });

  test('an impulse and a note after a long rest play as on the engine that never rested, with HALO and its late network', async () => {
    const [rested, awake] = await wake({ ...ECHO, halo: 0.7, mix: 0.8 }, 20000);
    const a = rested[0]!;
    const b = awake[0]!;
    expect(peak(b.left)).toBeGreaterThan(0.05);
    const worst = Math.max(largest(a.left, b.left), largest(a.right, b.right));
    expect(worst).toBeLessThan(1e-9);
    const steps = [maxSecondDifference(a.left).value, maxSecondDifference(b.left).value];
    expect(steps[0]!).toBeLessThanOrEqual(steps[1]! + 1e-9);
    note(
      `waking with HALO 0.7: the largest difference from the engine that never rested ${worst.toExponential(1)} (peak ${peak(b.left).toFixed(3)}); largest second difference ${steps[0]!.toFixed(6)} against ${steps[1]!.toFixed(6)}`,
    );
  });

  test('with the drift and the dark filter the heads stood still: the same sound within the drift, and no step', async () => {
    // a note shorter than the shorter side's time (2044 frames): its repeats do not lie on top of each other
    const [rested, awake] = await wake({ zone: 3, rate: 0.5, rpts: 0.5, colr: -1, skew: 0.3 }, 12000, 1500);
    const a = rested[0]!;
    const b = awake[0]!;
    // the note's first echo and its repeats: the same level, and nothing sharper than the engine that never rested
    const levels = [rms(a.left, 3000), rms(b.left, 3000), rms(a.right, 3000), rms(b.right, 3000)];
    expect(levels[0]! / levels[1]!).toBeGreaterThan(0.98);
    expect(levels[0]! / levels[1]!).toBeLessThan(1.02);
    expect(levels[2]! / levels[3]!).toBeGreaterThan(0.98);
    expect(levels[2]! / levels[3]!).toBeLessThan(1.02);
    // The sharpest place of both takes is the first echo's onset, read between two frames where the drift has the head (it
    // stood still on one engine and went on on the other): the same within a factor of 2. In every stretch of 1000 samples
    // after it the two engines are equally smooth.
    const steps = [maxSecondDifference(a.left, 3000).value, maxSecondDifference(b.left, 3000).value];
    expect(steps[0]!).toBeLessThan(2 * steps[1]!);
    for (const [x, y] of [
      [a.left, b.left],
      [a.right, b.right],
    ] as const)
      for (let from = 6000; from + 1000 <= x.length; from += 1000) {
        const [p, q] = [maxSecondDifference(x, from, from + 1000).value, maxSecondDifference(y, from, from + 1000).value];
        expect(p).toBeLessThan(1.15 * q + 1e-7);
      }
    // the impulse's echo is one sample on a whole frame or two between frames
    expect(peak(a.left, 0, 3000)).toBeGreaterThan(0.5 * peak(b.left, 0, 3000));
    note(
      `waking with the drift (COLR -1, SKEW 0.3): rms of the note's echoes ${levels[0]!.toFixed(5)} against ${levels[1]!.toFixed(5)} (right ${levels[2]!.toFixed(5)} against ${levels[3]!.toFixed(5)}); largest second difference ${steps[0]!.toFixed(6)} against ${steps[1]!.toFixed(6)}`,
    );
  });

  test('a control turned while resting runs one block, the status follows, and the next note plays as if it had been turned long ago', async () => {
    // free time: the engine kept awake glides to the new time in the silence; the rested one lands there
    const seen: number[] = [];
    const [rested, awake] = await both({ frames: 32768 }, async (rig, isAwake) => {
      rig.set({ zone: 4, rate: 0.5, rpts: 0.3, mix: 1 });
      await rig.run(60, NOTE);
      await settle(rig, isAwake, 12000);
      rig.set({ rate: 0.8, mix: 0.3, halo: 0.4 });
      const turned = await rig.run(1);
      const after = await rig.run(1800);
      if (!isAwake) {
        // the block of the change ran; the blocks after it rest
        seen.push(turned.instructions[0]!, Math.max(...after.instructions.slice(1)), after.instructions[0]!);
        const time = (await rig.status())[8]!;
        expect(Math.abs(time - zoneTime(4, 0.8))).toBeLessThan(2);
        expect(await word(rig, PS.CH_L + CH.A_DINT)).toBe(time);
        expect(await word(rig, PS.CH_L + CH.TARGET)).toBe(time);
        expect(await word(rig, PS.CH_L + CH.A_FRAC)).toBe(0);
        expect(await resting(rig)).toBe(true);
      }
      const from = rig.clock;
      return [await rig.run(900, burst(HZ, 0.3, from, 4000))];
    });
    expect(seen[0]!).toBeGreaterThan(RUN_LEAST);
    expect(seen[1]!).toBeLessThan(REST_MOST);
    const a = rested[0]!;
    const b = awake[0]!;
    expect(peak(b.left)).toBeGreaterThan(0.02);
    const worst = Math.max(largest(a.left, b.left), largest(a.right, b.right));
    expect(worst).toBeLessThan(1e-9);
    note(
      `RATE, MIX and HALO turned while resting: that block ${seen[0]} instructions, the next ${seen[2]}, then ${seen[1]}; the next note against the engine that never rested: largest difference ${worst.toExponential(1)} (peak ${peak(b.left).toFixed(3)})`,
    );
  });

  test('FLIP and HOLD while resting wake the engine, and it plays the next note backwards and holds it', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      rig.set(ECHO);
      await untilRest(rig, 1200);
      await rig.run(30);
      rig.set({ flip: true });
      expect((await rig.run(1)).instructions[0]!).toBeGreaterThan(RUN_LEAST);
      await untilRest(rig, 400);
      const from = rig.clock;
      const take = await rig.run(300, burst(HZ, 0.3, from, 1600));
      expect(take.instructions[0]!).toBeGreaterThan(RUN_LEAST);
      expect(peak(take.left)).toBeGreaterThan(0.1);
      rig.set({ hold: true });
      await rig.run(200, sine(HZ, 0.2));
      const held = await rig.run(300);
      expect(await word(rig, PS.HOLD)).toBe(2);
      expect(await word(rig, PS.IDLE)).toBe(0);
      expect(rms(held.left)).toBeGreaterThan(0.01);
    });
  });
});

describe('PRISM rest: what the ColdFire and the loop effect see', () => {
  test('the status mailbox of a resting engine is the one of the engine that never rested, word for word, every block', async () => {
    const lists: number[][][] = [];
    await both({ frames: 16384 }, async (rig, isAwake) => {
      rig.set({ ...ECHO, skew: 0.5 });
      await rig.run(60, NOTE);
      const list: number[][] = [];
      // the block that lets the engine rest, and the resting blocks after it
      const quiet = await settle(rig, isAwake, 12000, 0);
      list.push(await rig.status());
      let rests = 0;
      for (let b = 0; b < 600; b++) {
        const take = await rig.run(1);
        if (take.instructions[0]! < REST_MOST) rests += 1;
        if (b < 3 || b % 97 === 0) list.push(await rig.status());
      }
      expect(rests).toBe(isAwake ? 0 : 600);
      lists.push(list);
      return quiet;
    });
    const [rested, awake] = lists;
    expect(rested).toEqual(awake);
    // the heartbeat moves every block and closes the mailbox; READY; the two times; the whole tape
    const [first, second] = rested!;
    expect(second![2]).toBe(first![2]! + 1);
    expect(second![31]).toBe(second![2]);
    expect(second![3]).toBe(2);
    expect([second![8], second![9], second![10], second![11]]).toEqual([1920, 3000, 0, 16384]);
  });

  test('while PRISM rests it leaves the wet mailbox alone; on waking its output is in the mailbox at once', async () => {
    const [rested, awake] = await both({ frames: 16384 }, async (rig, isAwake) => {
      rig.loop = { alive: true, tap: silence, audible: false };
      rig.set(ECHO);
      const takes = [await rig.run(60, NOTE), ...(await settle(rig, isAwake, 12000)).slice(-1)];
      if (!isAwake) {
        expect(await resting(rig)).toBe(true);
        const seq = rig.wetSeq;
        const words = await rig.wetWords();
        await rig.run(100);
        expect(rig.wetSeq).toBe(seq);
        expect((await rig.wetWords())[LOOP.LW_SEQ!]).toBe(words[LOOP.LW_SEQ!]);
        // the status still says where the output goes, and the share stands at the loop effect
        expect((await rig.status())[15]).toBe(1);
        expect(w2f(await word(rig, PS.DIVERT))).toBe(1);
      } else await rig.run(100);
      const from = rig.clock;
      takes.push(await rig.run(300, burst(HZ, 0.3, from, 1600)));
      return takes;
    });
    const a = rested.at(-1)!;
    const b = awake.at(-1)!;
    expect(peak(a.left)).toBe(0);
    expect(peak(b.wetLeft)).toBeGreaterThan(0.05);
    expect(Math.max(largest(a.wetLeft, b.wetLeft), largest(a.wetRight, b.wetRight))).toBeLessThan(1e-9);
  });

  test('the loop effect stopping or starting while PRISM rests is followed: the status, and the output on the return or in the mailbox at once', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      rig.loop = { alive: true, tap: silence, audible: false };
      rig.set(ECHO);
      await untilRest(rig, 1200);
      rig.loop = undefined;
      const stopped = await rig.run(20);
      expect(Math.max(...stopped.instructions)).toBeLessThan(REST_MOST);
      expect((await rig.status())[15]).toBe(0);
      expect(await word(rig, PS.DIVERT)).toBe(0);
      let from = rig.clock;
      const onReturn = await rig.run(200, burst(HZ, 0.3, from, 1600));
      expect(peak(onReturn.left)).toBeGreaterThan(0.05);
      expect(peak(onReturn.wetLeft)).toBe(0);
      await untilRest(rig, 6000);
      rig.loop = { alive: true, tap: silence, audible: false };
      expect(Math.max(...(await rig.run(20)).instructions)).toBeLessThan(REST_MOST);
      from = rig.clock;
      const inMailbox = await rig.run(200, burst(HZ, 0.3, from, 1600));
      expect(peak(inMailbox.left)).toBe(0);
      expect(peak(inMailbox.wetLeft)).toBeGreaterThan(0.05);
    });
  });

  test('INPT at LOOP: a silent tap or none lets the engine rest, a tap that sounds wakes it, and a new loop is taken at once', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      const tone = sine(HZ, 0.2);
      rig.loop = { alive: true, tap: silence };
      rig.set({ ...ECHO, input: 1 });
      // the send bus sounds all the time: at LOOP it is not the input
      await untilRest(rig, 8000, tone);
      expect(Math.max(...(await rig.run(50, tone)).instructions)).toBeLessThan(REST_MOST);
      rig.loop = { alive: true, tap: tone, audible: false };
      expect(Math.max(...(await rig.run(50, tone)).instructions)).toBeLessThan(REST_MOST);
      rig.loop = { alive: true, tap: tone };
      const woke = await rig.run(200, tone);
      expect(woke.instructions[0]!).toBeGreaterThan(RUN_LEAST);
      expect(peak(woke.wetLeft, 3200)).toBeGreaterThan(0.05);
      // back at LIVE with a silent bus, resting; a new loop moves the input to LOOP by itself in the block after it
      rig.loop = { alive: true, tap: silence };
      rig.set({ input: 0 });
      await untilRest(rig, 6000);
      rig.loop = { alive: true, tap: tone, count: 1 };
      const taken = await rig.run(3);
      expect(taken.instructions[0]!).toBeGreaterThan(RUN_LEAST);
      expect((await rig.status())[12]).toBe(1);
      const left = await rig.tapeLeft(((await word(rig, PS.WPOS)) + 16384 - 3 * BLOCK) % 16384, 3 * BLOCK);
      expect(peak(left)).toBeGreaterThan(0.05);
    });
  });

  test('coupled with Overloop: while PRISM rests Overloop adds nothing from the mailbox, and its block is shorter', async () => {
    const pair = await PairRig.open({ prismFrames: 16384 });
    try {
      pair.prism.set(ECHO);
      const sounding = await pair.run(60, (n) => NOTE(n));
      for (let b = 0; b < 12000 && !(await resting(pair.prism)); b++) await pair.run(1, silence);
      // PRISM plays silence in full: Overloop takes its mailbox every block
      expect(await resting(pair.prism)).toBe(true);
      const rest = await pair.run(100, silence);
      expect(Math.max(...rest.prismInstructions)).toBeLessThan(REST_MOST);
      expect(peak(rest.left)).toBe(0);
      const taking = Math.min(...sounding.loopInstructions.slice(20));
      const skipping = Math.max(...rest.loopInstructions);
      expect(skipping).toBeLessThan(taking - 64 * 8);
      const from = pair.clock;
      const woke = await pair.run(200, burst(HZ, 0.3, from, 1600));
      expect(peak(woke.wetLeft)).toBeGreaterThan(0.01);
      expect(Math.min(...woke.loopInstructions.slice(1))).toBeGreaterThanOrEqual(taking);
      note(`coupled with Overloop: its block ${taking} instructions while PRISM plays, ${skipping} while PRISM rests`);
    } finally {
      await pair.close();
    }
  });
});
