/**
 * PRISM's repeater engine on the SHARC-only rig, behind the production effect adapter (docs/findings/358). Every claim of
 * the engine has a number here. The click measure is the largest second difference of the wet output around an event,
 * against the same measure in a steady stretch of the same take (docs/findings/354 "Clicks").
 */
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { afterAll, describe, expect, test } from 'vitest';
import {
  CH,
  DEVICE_FRAMES,
  PS,
  PrismRig,
  WFADE,
  XFADE,
  ZONES,
  bandLevel,
  burst,
  crossingHz,
  db,
  envelope,
  levelAt,
  maxSecondDifference,
  noise,
  peak,
  rms,
  runSteps,
  silence,
  sine,
  sum,
  w2f,
  zoneTime,
  type Controls,
  type Signal,
  type Take,
} from './engine-rig.ts';

const HZ = 220;
// below the limiter's knee also with the repeats on top of it, so a steady stretch is a clean tone
const TONE = sine(HZ, 0.08);
const DECLARED = Number(
  /^dsp_cost_fx\s*=\s*(\d+)/m.exec(readFileSync(path.join(import.meta.dirname, '../module.toml'), 'utf8'))?.[1],
);

/** The numbers of this run, printed at the end: the report a pin or a threshold is set from. */
const measures: string[] = [];
const note = (line: string): void => void measures.push(line);
afterAll(() => {
  console.info(
    ['PRISM engine measures:', ...measures, `heaviest block: ${PrismRig.worst} instructions (${PrismRig.worstIn})`].join(
      '\n  ',
    ),
  );
});

async function withRig<T>(o: Parameters<typeof PrismRig.open>[0], body: (rig: PrismRig) => Promise<T>): Promise<T> {
  const rig = await PrismRig.open(o);
  try {
    return await body(rig);
  } finally {
    await rig.close();
  }
}

/** One sample of `amp` at frame `at`. */
const impulse =
  (at: number, amp: number): Signal =>
  (n) =>
    n === at ? amp : 0;

/** The frame of the largest sample in [from, to). */
function peakAt(x: Float32Array, from = 0, to = x.length): number {
  let at = from;
  for (let n = from; n < to; n++) if (Math.abs(x[n]!) > Math.abs(x[at]!)) at = n;
  return at;
}

/**
 * The click measure of a take: for each event (marks[1..]), the largest second difference from the event to the next one,
 * as a multiple of the largest in the `quiet` samples before the first event. Both sides; the larger ratio counts.
 */
function clicks(take: Take, marks: number[], quiet = 4800): number[] {
  const first = marks[1]!;
  return marks.slice(1).map((m, i) => {
    const end = marks[i + 2] ?? take.left.length;
    return Math.max(
      ...[take.left, take.right].map(
        (x) => maxSecondDifference(x, m - 2, end - 2).value / maxSecondDifference(x, first - quiet, first - 2).value,
      ),
    );
  });
}

const fixed = (values: number[], digits = 2): string => values.map((v) => v.toFixed(digits)).join(' ');

describe('PRISM repeater engine: contract', () => {
  test('through every mode the module stores only to its output, its state and its bank, under a hostile context', async () => {
    await withRig({ frames: 16384, stores: true }, async (rig) => {
      const walk: [number, Partial<Controls>][] = [
        [300, { zone: 4, rate: 0.5, rpts: 0.6 }],
        [60, { zone: 3 }],
        [60, { flip: true }],
        [80, { hold: true }],
        [60, { rate: 0.2 }],
        [40, { flip: false }],
        [80, { hold: false }],
        [60, { colr: -1, halo: 1, rpts: 1, skew: 0.7 }],
        [60, { colr: 1, sync: true, syncTime: 640 }],
        [60, { hold: true, flip: true, halo: 0 }],
        [60, { hold: false, flip: false, colr: 0 }],
      ];
      const { take } = await runSteps(rig, TONE, walk);
      expect(rig.foreign).toEqual([]);
      expect(take.faulted).toBe(0);
      expect(peak(take.left)).toBeGreaterThan(0.05);
    });
  });

  test.each([1, 2, 3])('a random walk %i through the controls never reads tape that was not written', async (seed) => {
    // The bank starts as NaN words: a head that read a frame nobody wrote would put a NaN into the output, which the run
    // refuses. The walk starts on an empty tape (the time when most of it is unwritten) and turns every control at random
    // moments, HOLD and FLIP at the shortest and the longest times included.
    await withRig({ frames: 8192, poison: true, stores: true }, async (rig) => {
      let state = seed * 0x9e3779b1;
      const random = () => {
        state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
        return state / 2 ** 32;
      };
      const pick = <T>(values: readonly T[]): T => values[Math.floor(random() * values.length)]!;
      let level = 0;
      for (let step = 0; step < 400; step++) {
        const change: Partial<Controls> = {};
        const turns = 1 + Math.floor(random() * 3);
        for (let k = 0; k < turns; k++) {
          const which = Math.floor(random() * 10);
          if (which === 0) change.zone = 1 + Math.floor(random() * 8);
          else if (which === 1) change.rate = pick([0, 1, random()]);
          else if (which === 2) change.sync = random() < 0.5;
          else if (which === 3) change.syncTime = pick([1, 32, 33, 64, 97, 480, 2400, 4000, 4096, 100000]);
          else if (which === 4) change.flip = random() < 0.5;
          else if (which === 5) change.hold = random() < 0.5;
          else if (which === 6) change.skew = pick([-1, 0, 1, 2 * random() - 1]);
          else if (which === 7) change.rpts = pick([0, 0.75, 1, random()]);
          else if (which === 8) change.colr = pick([-1, 0, 1, 2 * random() - 1]);
          else change.halo = pick([0, 1, random()]);
        }
        rig.set(change);
        const take = await rig.run(pick([1, 1, 2, 3, 7, 16, 40]), sum(sine(HZ, 0.4), noise(0.1, seed + step)));
        level = Math.max(level, peak(take.left), peak(take.right));
      }
      expect(rig.foreign).toEqual([]);
      expect(level).toBeGreaterThan(0.1);
      expect(level).toBeLessThan(4);
    });
  });

  test('the descriptor is the six words of dk_fx.inc, with no parameters and no stack', async () => {
    await withRig({}, async (rig) => {
      const desc = rig.fx.descWords('_prism_desc');
      expect(desc[0]).toBe(0x444b4658);
      expect(desc[1]).toBe(1);
      expect(desc[2]).toBe(0);
      // the state: a multiple of 4 (the store check above bounds every store of the engine by it)
      expect(desc[3]! % 4).toBe(0);
      expect(desc[3]).toBeGreaterThan(0);
      expect(desc[4]).toBeGreaterThanOrEqual(1);
      expect(desc[4]).toBeLessThanOrEqual(8);
      expect(desc[5]).toBe(0);
    });
  });

  test.each([
    ['a control mailbox of another version', 4, 4, (rig: PrismRig) => rig.pokeControl(1, 2)],
    ['a control that is not a finite number', 4, 4, (rig: PrismRig) => rig.pokeControl(13, 0x7fc00000)],
    ['a mailbox that is not enabled', 4, 4, (rig: PrismRig) => rig.pokeControl(4, 0)],
    ['a bank whose frames are no multiple of 32', 0, 4, (rig: PrismRig) => rig.pokeStorage(5, 16400)],
    ['a bank shorter than its two planes', 0, 4, (rig: PrismRig) => rig.pokeStorage(4, 8 * 16384 - 4)],
    ['a bank outside external memory', 0, 4, (rig: PrismRig) => rig.pokeStorage(2, 0x200a0000)],
    // the adapter zeroes a faulted effect's state, so the engine then starts again on the new lease
    ['a lease that changed under the engine', 4, 1, (rig: PrismRig) => rig.pokeStorage(5, 8192)],
  ])('%s is a fault: the block plays dry', async (_name, goodBlocks, faults, breakIt) => {
    await withRig({ faults: true }, async (rig) => {
      const good = await rig.run(goodBlocks, TONE);
      expect(good.faulted).toBe(0);
      breakIt(rig);
      // the run checks that a faulted block leaves the master as it was
      const bad = await rig.run(4, TONE);
      expect(bad.faulted).toBe(faults);
    });
  });

  test('the status mailbox carries the heartbeat, the two times and the hold state', async () => {
    await withRig({}, async (rig) => {
      rig.set({ sync: true, syncTime: 2400, skew: 1 });
      await rig.run(120, TONE);
      let status = await rig.status();
      expect(status[0]).toBe(0x50485331);
      expect(status[1]).toBe(3);
      expect(status[2]).toBe(status[31]);
      expect(status[3]).toBe(2);
      expect([status[8], status[9], status[10]]).toEqual([1600, 3600, 0]);
      rig.set({ hold: true });
      await rig.run(40, TONE);
      status = await rig.status();
      expect(status[3]).toBe(5);
      expect(status[10]).toBe(2);
    });
  });

  test('the tables in the data are the formulas of their comments', async () => {
    await withRig({}, async (rig) => {
      const floats = async (symbol: string, count: number) =>
        (await rig.fx.peek(rig.fx.built.data[symbol]!, count)).map((w) => w2f(w ?? 0));
      const fade = await floats('_prism_fade', 129);
      for (let k = 0; k <= 128; k++) {
        const u = k / 128;
        expect(fade[k]).toBeCloseTo(Math.sin((Math.PI / 2) * (3 * u * u - 2 * u ** 3)), 6);
        // equal power: the gain that comes and the gain that goes
        expect(fade[k]! ** 2 + fade[128 - k]! ** 2).toBeCloseTo(1, 6);
      }
      expect([fade[0], fade[128]]).toEqual([0, 1]);
      const zones = await floats('_prism_zones', 16);
      ZONES.forEach(([low, high], z) => {
        expect(zones[2 * z]).toBe(low);
        expect(zones[2 * z + 1]).toBeCloseTo(Math.log(high / low), 6);
      });
    });
  });
});

describe('PRISM repeater engine: time', () => {
  test('ZONE and RATE give low * (high / low) ^ RATE frames in every zone, up to 10.7 s in one bank', async () => {
    await withRig({ frames: DEVICE_FRAMES }, async (rig) => {
      const rows: string[] = [];
      for (let zone = 1; zone <= 8; zone++) {
        const got: number[] = [];
        for (const rate of [0, 0.5, 1]) {
          rig.set({ zone, rate });
          await rig.run(1);
          const [time] = await rig.state(PS.TIME_L);
          const want = zoneTime(zone, rate);
          expect(Math.abs(time! - want)).toBeLessThanOrEqual(0.5 + 2e-5 * want);
          got.push(time!);
        }
        rows.push(`${zone}: ${got.map((t) => `${((t * 1000) / 48000).toFixed(1)} ms`).join(', ')}`);
      }
      note(`zones (RATE 0, 0.5, 1): ${rows.join('; ')}`);
    });
  });

  test('the echo arrives exactly TIME frames late, free and synced, at the shortest and a long time', async () => {
    await withRig({ frames: DEVICE_FRAMES }, async (rig) => {
      const cases: [string, Partial<Controls>, number][] = [
        ['zone 1, RATE 0', { zone: 1, rate: 0 }, 48],
        ['zone 3, RATE 0.5', { zone: 3, rate: 0.5 }, Math.round(zoneTime(3, 0.5))],
        ['zone 5, RATE 1', { zone: 5, rate: 1 }, 48000],
        ['SYNC 9000', { sync: true, syncTime: 9000, zone: 2 }, 9000],
      ];
      for (const [name, controls, frames] of cases) {
        rig.set({ sync: false, rpts: 0, ...controls });
        // the tape has to hold TIME frames before the head can read it
        await rig.run(Math.ceil((frames + 2400) / 32));
        const at = rig.clock + 40;
        const origin = rig.clock;
        const take = await rig.run(Math.ceil((frames + 400) / 32), impulse(at, 0.5));
        expect(peakAt(take.left) + origin - at, name).toBe(frames);
        expect(peakAt(take.right) + origin - at, name).toBe(frames);
        expect(Math.abs(take.left[peakAt(take.left)]!), name).toBeCloseTo(0.5, 3);
      }
    });
  });

  test('the longest time, zone 8 at RATE 1 (10.7 s), plays from one bank of the device', async () => {
    await withRig({ frames: DEVICE_FRAMES }, async (rig) => {
      rig.set({ zone: 8, rate: 1, rpts: 0 });
      const at = 513600 + 4000;
      const take = await rig.run(Math.ceil((at + 513600 + 2000) / 32), burst(440, 0.25, at, 960));
      const [time] = await rig.state(PS.TIME_L);
      expect(Math.abs(time! - 513600)).toBeLessThanOrEqual(8);
      // nothing before the echo, and the echo at the level of the burst
      expect(peak(take.left, 0, at + time! - 10)).toBe(0);
      const echo = rms(take.left, at + time!, at + time! + 960);
      expect(echo / rms(take.dryLeft, at, at + 960)).toBeCloseTo(1, 3);
      note(`longest time: ${time} frames (${(time! / 48000).toFixed(3)} s), echo / burst ${(echo / rms(take.dryLeft, at, at + 960)).toFixed(4)}`);
    });
  });

  test('SKEW makes one side shorter and the other longer by the same ratio, up to 1.5', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      const rows: string[] = [];
      for (const [skew, left, right] of [
        [0, 2400, 2400],
        [0.5, 1920, 3000],
        [1, 1600, 3600],
        [-1, 3600, 1600],
      ] as const) {
        rig.set({ sync: true, syncTime: 2400, rpts: 0, skew });
        await rig.run(400);
        const at = rig.clock + 40;
        const origin = rig.clock;
        const take = await rig.run(130, impulse(at, 0.5));
        const lags = [peakAt(take.left) + origin - at, peakAt(take.right) + origin - at];
        expect(lags).toEqual([left, right]);
        rows.push(`${skew}: ${lags.join(' / ')}`);
      }
      note(`SKEW at 2400 frames (left / right): ${rows.join('; ')}`);
    });
  });
});

describe('PRISM repeater engine: a free move bends the pitch, a jump does not', () => {
  test('a free RATE move glides: the pitch falls to a half going longer and rises by a half going shorter', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      rig.set({ zone: 4, rate: 0.3, rpts: 0 });
      const from = Math.round(zoneTime(4, 0.3));
      const to = Math.round(zoneTime(4, 0.6));
      const glide = (to - from) * 2;
      await rig.run(600, sine(1000, 0.25));
      rig.set({ rate: 0.6 });
      const up = await rig.run(600, sine(1000, 0.25));
      // the delay never moves faster than half a frame a sample, and it comes to rest on the whole frame
      rig.set({ rate: 0.3 });
      const delays: number[] = [(await rig.state(PS.CH_L + CH.A_DINT))[0]!];
      const takes: Take[] = [];
      for (let b = 0; b < 600; b++) {
        takes.push(await rig.run(1, sine(1000, 0.25)));
        const [dint, frac] = await rig.state(PS.CH_L + CH.A_DINT, 2);
        delays.push(dint! + w2f(frac!));
      }
      const down = Float32Array.from(takes.flatMap((t) => [...t.left]));
      const moves = delays.slice(1).map((d, i) => Math.abs(d - delays[i]!));
      expect(Math.max(...moves)).toBeLessThanOrEqual(16.001);
      expect(Math.max(...moves)).toBeGreaterThan(15.9);
      expect(delays.at(-1)).toBe(from);
      // the bend eases in: in the first block the delay has moved by a small part of a block's full glide
      expect(moves[0]!).toBeLessThan(2);
      const longer = crossingHz(up.left, Math.round(glide * 0.3), Math.round(glide * 0.7));
      const shorter = crossingHz(down, Math.round(glide * 0.3), Math.round(glide * 0.7));
      const rest = crossingHz(down, glide + 6000, glide + 10000);
      expect(longer).toBeGreaterThan(495);
      expect(longer).toBeLessThan(505);
      expect(shorter).toBeGreaterThan(1495);
      expect(shorter).toBeLessThan(1505);
      expect(rest).toBeCloseTo(1000, 1);
      note(`free RATE ${from} -> ${to} -> ${from} frames with a 1 kHz tone: ${longer.toFixed(1)} Hz going longer, ${shorter.toFixed(1)} Hz going shorter, ${rest.toFixed(2)} Hz at rest`);
    });
  });

  test('a zone change and a SYNC step keep the pitch: the tone stays at its frequency through the crossfade', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      rig.set({ zone: 4, rate: 0.3, rpts: 0 });
      const { take, marks } = await runSteps(rig, sine(1000, 0.25), [
        [600, {}],
        [300, { zone: 5, rate: 0 }],
        [300, { zone: 3, rate: 0.9 }],
        [300, { sync: true, syncTime: 7200 }],
        [300, { syncTime: 3600 }],
      ]);
      const rows: string[] = [];
      for (const m of marks.slice(1)) {
        // during the crossfade the two heads' tones add with their phases, which reads as at most half a cycle more or
        // less over its length (25 Hz); a glide would read 500 or 1500 Hz
        const during = crossingHz(take.left, m, m + XFADE);
        const after = crossingHz(take.left, m + XFADE + 200, m + 9000);
        expect(Math.abs(during - 1000)).toBeLessThan(30);
        expect(after).toBeCloseTo(1000, 1);
        expect(levelAt(take.left, 1000, m, m + XFADE + 2000)).toBeGreaterThan(0.15);
        for (const bent of [500, 1500]) expect(levelAt(take.left, bent, m, m + XFADE + 2000)).toBeLessThan(0.005);
        rows.push(`${during.toFixed(1)}/${after.toFixed(2)}`);
      }
      note(`jumps with a 1 kHz tone, Hz during the crossfade / after it: ${rows.join(' ')}`);
    });
  });
});

describe('PRISM repeater engine: no clicks', () => {
  // A crossfade adds two heads' tones with their phases: the level can rise to sqrt(2), and the measure with it.
  const LIMIT = 2;

  test('zone changes', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      rig.set({ zone: 4, rate: 0.5, rpts: 0.5 });
      const { take, marks } = await runSteps(rig, TONE, [
        [900, {}],
        [300, { zone: 5, rate: 0.1 }],
        [300, { zone: 3 }],
        [300, { zone: 1, rate: 0 }],
        [300, { zone: 4, rate: 0.5 }],
      ]);
      const got = clicks(take, marks);
      note(`clicks, zone 4 -> 5 -> 3 -> 1 -> 4: ${fixed(got)} x steady`);
      for (const ratio of got) expect(ratio).toBeLessThan(LIMIT);
    });
  });

  test('SYNC steps and SYNC on and off', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      rig.set({ sync: true, syncTime: 2400, rpts: 0.5 });
      const { take, marks } = await runSteps(rig, TONE, [
        [900, {}],
        [300, { syncTime: 3600 }],
        [300, { syncTime: 1200 }],
        [300, { sync: false, zone: 3, rate: 0.5 }],
        [300, { sync: true }],
      ]);
      const got = clicks(take, marks);
      for (const ratio of got) expect(ratio).toBeLessThan(LIMIT);
      note(`clicks, SYNC 2400 -> 3600 -> 1200 -> free -> SYNC: ${fixed(got)} x steady`);
    });
  });

  test('FLIP on and off, and every reverse segment', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      rig.set({ sync: true, syncTime: 2400, rpts: 0.5 });
      const { take, marks } = await runSteps(rig, TONE, [
        [900, {}],
        [600, { flip: true }],
        [600, { flip: false }],
        [600, { flip: true, syncTime: 480 }],
        [600, { flip: false }],
      ]);
      const got = clicks(take, marks);
      for (const ratio of got) expect(ratio).toBeLessThan(LIMIT);
      note(`clicks, FLIP on -> off -> on at 480 frames -> off (the segments' seams included): ${fixed(got)} x steady`);
    });
  });

  test('HOLD on and off, every turn of the held loop, and ZONE, RATE and FLIP while held', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      rig.set({ sync: true, syncTime: 2400, rpts: 0.5 });
      const { take, marks } = await runSteps(rig, TONE, [
        [900, {}],
        [600, { hold: true }],
        [600, { hold: false }],
        [600, { hold: true }],
        [300, { syncTime: 1200 }],
        [300, { syncTime: 7200 }],
        [300, { sync: false, zone: 2, rate: 0.4 }],
        [300, { flip: true }],
        [300, { flip: false, sync: true, syncTime: 2400 }],
        [600, { hold: false }],
      ]);
      const got = clicks(take, marks);
      for (const ratio of got) expect(ratio).toBeLessThan(LIMIT);
      note(`clicks, HOLD on -> off -> on -> shorter -> longer -> zone 2 -> FLIP -> back -> off: ${fixed(got)} x steady`);
    });
  });

  test('SKEW and free RATE moves', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      // one echo: with repeats a glide bends what an earlier glide bent, and the measure would follow the pitch
      rig.set({ zone: 4, rate: 0.2, rpts: 0 });
      const list: [number, Partial<Controls>][] = [[900, {}]];
      for (let k = 1; k <= 16; k++) list.push([20, { skew: k / 16 }]);
      for (let k = 15; k >= -16; k--) list.push([20, { skew: k / 16 }]);
      list.push([600, { skew: 0 }]);
      const rateFrom = list.length;
      for (let k = 1; k <= 24; k++) list.push([20, { rate: 0.2 + k / 80 }]);
      list.push([600, { rate: 0.2 }]);
      const { take, marks } = await runSteps(rig, TONE, list);
      const got = clicks(take, [marks[0]!, marks[1]!, marks[rateFrom]!]);
      note(`clicks, SKEW 0 -> 1 -> -1 -> 0 in steps, then RATE 0.2 -> 0.5 in steps -> 0.2 (the bend up raises a clean tone's measure by 2.25): ${fixed(got)} x steady`);
      // A glide to a shorter time plays up to 1.5 times faster: a clean tone's measure rises by 1.5 squared. A tenth more
      // is the linear interpolation between frames (every other sample of that glide is a mean of two frames).
      for (const ratio of got) expect(ratio).toBeLessThan(2.25 * 1.2);
    });
  });

  test('RPTS, COLR, HALO and MIX, in steps and at once from end to end', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      rig.set({ sync: true, syncTime: 2400, rpts: 0.5 });
      const list: [number, Partial<Controls>][] = [[900, {}]];
      const sweep = (name: 'colr' | 'halo' | 'rpts' | 'mix', values: number[]) => {
        for (const v of values) list.push([8, { [name]: v }]);
      };
      const ramp = (from: number, to: number) => Array.from({ length: 33 }, (_, k) => from + ((to - from) * k) / 32);
      sweep('colr', [...ramp(0, -1), ...ramp(-1, 1), ...ramp(1, 0)]);
      const halo = list.length;
      sweep('halo', [...ramp(0, 1), ...ramp(1, 0)]);
      const rpts = list.length;
      sweep('rpts', [...ramp(0.5, 0), ...ramp(0, 0.5)]);
      const mix = list.length;
      sweep('mix', [...ramp(1, 0), ...ramp(0, 1)]);
      const jumps = list.length;
      const atOnce = [{ colr: -1 }, { colr: 1 }, { colr: 0 }, { halo: 1 }, { halo: 0 }, { mix: 0 }, { mix: 1 }, { rpts: 0 }, { rpts: 0.5 }];
      for (const change of atOnce) list.push([120, change]);
      const { take, marks } = await runSteps(rig, TONE, list);
      const got = clicks(take, [marks[0]!, marks[1]!, marks[halo]!, marks[rpts]!, marks[mix]!, ...marks.slice(jumps)]);
      note(`clicks, COLR / HALO / RPTS / MIX in steps: ${fixed(got.slice(0, 4))} x steady; at once ${atOnce.map((c) => JSON.stringify(c).replace(/[{}"]/g, '')).join(', ')}: ${fixed(got.slice(4))} x steady`);
      for (const ratio of got) expect(ratio).toBeLessThan(LIMIT);
    });
  });

  test('the first echo after the start comes in without a step', async () => {
    await withRig({ frames: 16384 }, async (rig) => {
      rig.set({ sync: true, syncTime: 2400, rpts: 0.5 });
      const take = await rig.run(900, TONE);
      const first = take.left.findIndex((v) => v !== 0);
      const ratio = maxSecondDifference(take.left, 0, 6000).value / maxSecondDifference(take.left, 20000, 28000).value;
      expect(first).toBeGreaterThanOrEqual(2400);
      expect(ratio).toBeLessThan(1.2);
      note(`start: the head comes in at sample ${first}, ${ratio.toFixed(2)} x steady`);
    });
  });

  test('a crossfade keeps the level of unrelated material, and a reverse segment or a held loop has no dip', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      // noise: what the two heads read is unrelated, so equal power is a flat level (a linear crossfade would dip to 0.71)
      rig.set({ sync: true, syncTime: 2400, rpts: 0 });
      const input = noise(0.25);
      await rig.run(600, input);
      const steady = await rig.run(600, input);
      const middle: number[] = [];
      for (let k = 0; k < 24; k++) {
        rig.set({ syncTime: k % 2 ? 2400 : 3700 });
        const take = await rig.run(60, input);
        middle.push(rms(take.left, XFADE / 2 - 120, XFADE / 2 + 120));
      }
      const mid = Math.sqrt(middle.reduce((s, v) => s + v * v, 0) / middle.length) / rms(steady.left);
      expect(mid).toBeGreaterThan(0.93);
      expect(mid).toBeLessThan(1.07);
      rig.set({ syncTime: 2400 });
      await rig.run(200, input);
      const forward = envelope((await rig.run(900, input)).left, 0, 28800);
      rig.set({ flip: true });
      await rig.run(200, input);
      const reverse = envelope((await rig.run(900, input)).left, 0, 28800);
      rig.set({ flip: false, hold: true });
      await rig.run(200, input);
      const held = envelope((await rig.run(900, input)).left, 0, 28800);
      // 5 ms windows of noise vary by themselves (the forward head shows by how much); a dip would go far below
      for (const e of [reverse, held]) {
        expect(e.low).toBeGreaterThan(forward.low - 0.05);
        expect(e.median).toBeCloseTo(forward.median, 2);
      }
      note(`level through a crossfade of noise: ${mid.toFixed(3)} of steady; lowest 5 ms window against the median: forward ${forward.low.toFixed(3)}, reverse ${reverse.low.toFixed(3)}, held ${held.low.toFixed(3)}`);
    });
  });
});

describe('PRISM repeater engine: repeats', () => {
  test('RPTS sets the level from one repeat to the next: 0 is one echo, 0.75 holds for ever, above it the level grows', async () => {
    const T = 2400;
    const rows: string[] = [];
    for (const [rpts, ratio] of [
      [0, 0],
      [0.25, 1 / 3],
      [0.5, 2 / 3],
      [0.75, 1],
      [1, 1.1],
    ] as const) {
      await withRig({ frames: 65536 }, async (rig) => {
        rig.set({ sync: true, syncTime: T, rpts });
        await rig.run(300);
        const at = 64;
        const take = await rig.run(1300, burst(440, 0.2, rig.clock + at, 960));
        const level = (k: number) => rms(take.left, at + k * T, at + k * T + 960);
        const first = level(1);
        expect(first / rms(take.dryLeft, at, at + 960)).toBeCloseTo(1, 3);
        if (rpts === 0) {
          expect(level(2) / first).toBeLessThan(1e-3);
        } else if (rpts < 1) {
          for (let k = 2; k <= 6; k++) expect(level(k) / level(k - 1)).toBeCloseTo(ratio, 3);
        } else {
          // 1.1 a repeat until the limiter's knee, then less and less
          expect(level(2) / first).toBeCloseTo(1.1, 2);
          expect(level(16) / level(15)).toBeLessThan(1.04);
          expect(peak(take.left)).toBeLessThanOrEqual(0.51);
        }
        if (rpts === 0.75) expect(level(16) / first).toBeCloseTo(1, 3);
        rows.push(`${rpts}: ${rpts === 0 ? (level(2) / first).toExponential(1) : (level(2) / first).toFixed(4)}${rpts >= 0.75 ? ` (16th / 1st ${(level(16) / first).toFixed(4)})` : ''}`);
      });
    }
    note(`RPTS, level of a repeat against the one before: ${rows.join('; ')}`);
  });

  test('at the top of RPTS the level stays bounded for 30 s, with the input and after it', async () => {
    await withRig({ frames: 16384, poison: true }, async (rig) => {
      rig.set({ sync: true, syncTime: 4800, rpts: 1 });
      const input = sine(HZ, 0.5);
      const withInput = await rig.run(30000, input);
      const after = await rig.run(15000, silence);
      // the tape holds the input and at most the limiter's ceiling (0.5) of feedback; the DC blocker's edge adds a little
      expect(peak(withInput.left)).toBeLessThanOrEqual(1.02);
      expect(peak(after.left, 9600)).toBeLessThanOrEqual(0.52);
      const middle = rms(after.left, 240000, 288000);
      const end = rms(after.left, 432000, 480000);
      expect(end / middle).toBeCloseTo(1, 2);
      note(`RPTS 1 for 30 s (20 s of a 0.5 tone, 10 s without): peak ${peak(withInput.left).toFixed(3)} with the input, ${peak(after.left, 9600).toFixed(3)} after it; rms ${middle.toFixed(4)} at 5 s and ${end.toFixed(4)} at 10 s after`);
    });
  }, 300_000);

  test('a repeat above the limiter\'s knee comes down toward it; one below it is not touched', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      const T = 2400;
      rig.set({ sync: true, syncTime: T, rpts: 0.75 });
      await rig.run(300);
      const at = 64;
      const take = await rig.run(1300, burst(440, 0.5, rig.clock + at, 960));
      const tops = [1, 2, 4, 16].map((k) => peak(take.left, at + k * T, at + k * T + 960));
      expect(tops[0]).toBeCloseTo(0.5, 2);
      expect(tops[1]!).toBeLessThan(0.47);
      expect(tops[3]!).toBeLessThan(tops[2]!);
      expect(tops[3]!).toBeGreaterThan(0.25);
      note(`RPTS 0.75 with a burst of 0.5: peaks of repeats 1, 2, 4, 16: ${fixed(tops, 3)}`);
    });
  });
});

describe('PRISM repeater engine: COLR', () => {
  /** dB from one repeat to the next of a low and a high tone, at unity repeats. */
  async function tilt(colr: number): Promise<{ low: number[]; high: number[] }> {
    return withRig({ frames: 65536 }, async (rig) => {
      const T = 4800;
      rig.set({ sync: true, syncTime: T, rpts: 0.75, colr });
      await rig.run(600);
      const at = 64;
      const start = rig.clock + at;
      const take = await rig.run(1400, sum(burst(200, 0.08, start, 3840, 480), burst(4000, 0.08, start, 3840, 480)));
      const levels = (lo: number, hi: number) =>
        Array.from({ length: 5 }, (_, k) => bandLevel(take.left, lo, hi, at + (k + 1) * T, at + (k + 1) * T + 3840));
      const steps = (l: number[]) => l.slice(1).map((v, k) => db(v / l[k]!));
      return { low: steps(levels(150, 250)), high: steps(levels(3600, 4400)) };
    });
  }

  test('at the centre the repeats keep their spectrum', async () => {
    const { low, high } = await tilt(0);
    for (const step of [...low, ...high]) expect(Math.abs(step)).toBeLessThan(0.01);
    note(`COLR 0, dB a repeat at 200 Hz / 4 kHz: ${low[0]!.toFixed(3)} / ${high[0]!.toFixed(3)}`);
  });

  test('toward dark every repeat loses highs, down to a low pass at 800 Hz', async () => {
    const quarter = await tilt(-0.25);
    const half = await tilt(-0.5);
    const most = await tilt(-0.75);
    const full = await tilt(-1);
    note(`COLR dark, dB a repeat at 200 Hz / 4 kHz: ${[['-0.25', quarter], ['-0.5', half], ['-0.75', most], ['-1', full]].map(([name, t]) => `${name as string}: ${(t as typeof full).low[0]!.toFixed(2)} / ${(t as typeof full).high[0]!.toFixed(2)}`).join('; ')}`);
    // a one-pole low pass at 800 Hz takes 14.2 dB at 4 kHz and 0.3 dB at 200 Hz. The amount is the square of COLR: at a
    // half the highs fall by 0.24 of what the low pass takes off them (2.4 dB)
    expect(full.high[0]!).toBeLessThan(-12);
    expect(full.high[0]!).toBeGreaterThan(-16);
    expect(half.high[0]!).toBeLessThan(-1.9);
    expect(half.high[0]!).toBeGreaterThan(-3);
    // more dark, more loss
    expect(quarter.high[0]!).toBeLessThan(-0.3);
    expect(half.high[0]!).toBeLessThan(quarter.high[0]!);
    expect(most.high[0]!).toBeLessThan(half.high[0]!);
    expect(full.high[0]!).toBeLessThan(most.high[0]!);
    for (const step of [...half.low, ...full.low]) expect(step).toBeGreaterThan(-0.6);
    for (const step of full.high) expect(step).toBeLessThan(-12);
  });

  test('toward bright every repeat loses lows, up to a high pass at 1 kHz', async () => {
    const quarter = await tilt(0.25);
    const half = await tilt(0.5);
    const most = await tilt(0.75);
    const full = await tilt(1);
    note(`COLR bright, dB a repeat at 200 Hz / 4 kHz: ${[['0.25', quarter], ['0.5', half], ['0.75', most], ['1', full]].map(([name, t]) => `${name as string}: ${(t as typeof full).low[0]!.toFixed(2)} / ${(t as typeof full).high[0]!.toFixed(2)}`).join('; ')}`);
    // a one-pole high pass at 1 kHz takes 14.2 dB at 200 Hz and 0.3 dB at 4 kHz; at a half the lows fall by 2.4 dB
    expect(full.low[0]!).toBeLessThan(-13);
    expect(full.low[0]!).toBeGreaterThan(-15.5);
    expect(half.low[0]!).toBeLessThan(-1.9);
    expect(half.low[0]!).toBeGreaterThan(-3);
    expect(quarter.low[0]!).toBeLessThan(-0.3);
    expect(half.low[0]!).toBeLessThan(quarter.low[0]!);
    expect(most.low[0]!).toBeLessThan(half.low[0]!);
    expect(full.low[0]!).toBeLessThan(most.low[0]!);
    for (const step of [...half.high, ...full.high]) expect(step).toBeGreaterThan(-0.5);
  });

  test('the dark side saturates gently and drifts slowly; the centre does neither', async () => {
    const run = (colr: number) =>
      withRig({ frames: 65536 }, async (rig) => {
        rig.set({ sync: true, syncTime: 12000, rpts: 0, colr });
        await rig.run(900, sine(1000, 0.8));
        const take = await rig.run(4500, sine(1000, 0.8));
        const pitch: number[] = [];
        for (let a = 0; a + 4800 <= take.left.length; a += 4800) pitch.push(crossingHz(take.left, a, a + 4800));
        return {
          third: db(levelAt(take.left, 3000, 0, 48000) / levelAt(take.left, 1000, 0, 48000)),
          level: rms(take.left) / rms(take.dryLeft),
          wobble: (Math.max(...pitch) - Math.min(...pitch)) / 2000,
        };
      });
    const centre = await run(0);
    const dark = await run(-1);
    expect(centre.third).toBeLessThan(-100);
    expect(centre.wobble).toBeLessThan(1e-5);
    // the dark side at a tone of 0.8: a third harmonic well below the tone, and a pitch that moves by about 0.15 %
    expect(dark.third).toBeGreaterThan(-60);
    expect(dark.third).toBeLessThan(-25);
    expect(dark.wobble).toBeGreaterThan(0.0005);
    expect(dark.wobble).toBeLessThan(0.002);
    note(`a 1 kHz tone of 0.8, one echo: third harmonic ${centre.third.toFixed(0)} dB at the centre, ${dark.third.toFixed(1)} dB at full dark; pitch drift +-${(dark.wobble * 100).toFixed(3)} % at full dark, ${(centre.wobble * 100).toFixed(4)} % at the centre`);
  });
});

describe('PRISM repeater engine: HALO', () => {
  test('HALO at 0 is the engine without it, bit for bit, on the output and on the tape', async () => {
    const scene = (haloOff: boolean) =>
      withRig({ frames: 16384, haloOff }, async (rig) => {
        rig.set({ zone: 3, rate: 0.5, rpts: 0.7, colr: -0.4, skew: 0.3, halo: 0 });
        const { take } = await runSteps(rig, sum(TONE, noise(0.05)), [
          [300, {}],
          [100, { zone: 4, rate: 0.2 }],
          [100, { flip: true }],
          [100, { flip: false, hold: true }],
          [100, { hold: false, rate: 0.6 }],
        ]);
        return { take, tape: await rig.tape() };
      });
    const without = await scene(true);
    const withHalo = await scene(false);
    expect(Buffer.from(withHalo.take.left.buffer).equals(Buffer.from(without.take.left.buffer))).toBe(true);
    expect(Buffer.from(withHalo.take.right.buffer).equals(Buffer.from(without.take.right.buffer))).toBe(true);
    expect(withHalo.tape).toEqual(without.tape);
    expect(peak(withHalo.take.left)).toBeGreaterThan(0.05);
  });

  test('with HALO up every repeat is smeared more than the last, and the repeats keep their energy', async () => {
    const T = 9600;
    const rows: string[] = [];
    const shares: number[][] = [];
    for (const halo of [0, 0.25, 0.5, 1]) {
      await withRig({ frames: 65536 }, async (rig) => {
        rig.set({ sync: true, syncTime: T, rpts: 0.75, halo });
        await rig.run(600);
        const at = 320;
        const take = await rig.run(2800, burst(1000, 0.2, rig.clock + at, 96, 24));
        // per repeat: its energy (both sides), and the share of it outside the burst's own 96 frames
        const energy = (from: number, to: number) => rms(take.left, from, to) ** 2 * (to - from) + rms(take.right, from, to) ** 2 * (to - from);
        const repeats = Array.from({ length: 8 }, (_, k) => {
          const start = at + (k + 1) * T;
          const all = energy(start - 200, start + T - 200);
          return { all, smeared: 1 - energy(start, start + 96) / all };
        });
        for (const r of repeats) expect(r.all / repeats[0]!.all).toBeCloseTo(1, 1);
        const share = repeats.map((r) => r.smeared);
        if (halo === 0) for (const s of share) expect(s).toBeLessThan(1e-4);
        else for (let k = 1; k < 6; k++) expect(share[k]!).toBeGreaterThan(share[k - 1]!);
        shares.push(share);
        rows.push(`${halo}: ${fixed(share.slice(0, 6), 3)} (energy of the 8th repeat ${(repeats[7]!.all / repeats[0]!.all).toFixed(3)} of the 1st)`);
      });
    }
    // more HALO, more of the first repeat is smeared
    expect(shares[2]![0]!).toBeGreaterThan(shares[1]![0]!);
    expect(shares[3]![0]!).toBeGreaterThan(shares[2]![0]!);
    note(`HALO, share of a repeat's energy outside the burst's 2 ms, repeats 1 to 6: ${rows.join('; ')}`);
  });

  test('turned back to 0, HALO leaves the path and its lines are empty when it is turned up again', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      rig.set({ sync: true, syncTime: 2400, rpts: 0, halo: 1 });
      await rig.run(600, TONE);
      rig.set({ halo: 0 });
      await rig.run(600, TONE);
      const dry = await rig.run(100, TONE);
      // one echo, no halo: the output is the input 2400 frames late (through the DC blocker)
      for (let n = 0; n < 3200; n += 97) expect(dry.left[n]).toBeCloseTo(0.08 * Math.sin((2 * Math.PI * HZ * (dry.start + n - 2400)) / 48000), 2);
      // silence, until the last echo and the DC blocker's tail are gone; lines that were not emptied would still hold the tone
      await rig.run(1500);
      rig.set({ halo: 1 });
      const quiet = await rig.run(200);
      expect(peak(quiet.left)).toBeLessThan(1e-6);
      expect(peak(quiet.right)).toBeLessThan(1e-6);
    });
  });
});

describe('PRISM repeater engine: FLIP', () => {
  test('the repeat plays backwards: a burst that swells comes back fading', async () => {
    const shape = (flip: boolean) =>
      withRig({ frames: 65536 }, async (rig) => {
        const T = 4800;
        rig.set({ sync: true, syncTime: T, rpts: 0, flip });
        await rig.run(900);
        // a tone that swells over 1200 frames and stops, inside one reverse segment: the head's delay is 32 + twice the
        // samples since its segment began
        const [delay] = await rig.state(PS.CH_L + CH.A_DINT);
        const at = rig.clock + (flip ? T - (delay! - 32) / 2 + 600 : 2464);
        const swell: Signal = (n) => (n >= at && n < at + 1200 ? ((n - at) / 1200) * 0.3 * Math.sin((2 * Math.PI * 1000 * (n - at)) / 48000) : 0);
        const take = await rig.run(600, swell);
        let first = -1;
        let last = -1;
        let energy = 0;
        let moment = 0;
        for (let n = 0; n < take.left.length; n++) {
          const p = take.left[n]! ** 2;
          if (p > 1e-6) {
            if (first < 0) first = n;
            last = n;
          }
          energy += p;
          moment += p * n;
        }
        return { length: last - first, centre: (moment / energy - first) / (last - first), level: Math.sqrt(energy) };
      });
    const forward = await shape(false);
    const reverse = await shape(true);
    // the energy of a linear swell sits at three quarters of its length; backwards at one quarter
    expect(forward.centre).toBeGreaterThan(0.7);
    expect(reverse.centre).toBeLessThan(0.3);
    expect(Math.abs(reverse.length - forward.length)).toBeLessThan(40);
    expect(reverse.level / forward.level).toBeCloseTo(1, 1);
    note(`a swelling burst, where its echo's energy sits (0 start, 1 end): forward ${forward.centre.toFixed(3)}, FLIP ${reverse.centre.toFixed(3)}; level ${(reverse.level / forward.level).toFixed(3)} of forward`);
  });
});

describe('PRISM repeater engine: HOLD', () => {
  test('taking HOLD is not heard: the output goes on exactly as without it until the loop turns', async () => {
    const T = 4800;
    const scene = (hold: boolean) =>
      withRig({ frames: 65536 }, async (rig) => {
        rig.set({ sync: true, syncTime: T, rpts: 0.5 });
        await rig.run(900, sum(TONE, noise(0.05)));
        rig.set({ hold });
        return rig.run(150, sum(TONE, noise(0.05)));
      });
    const held = await scene(true);
    const running = await scene(false);
    // the loop's seam starts its crossfade XFADE samples before the loop's end
    const same = T - XFADE - 1;
    expect(Buffer.from(held.left.buffer, 0, 4 * same).equals(Buffer.from(running.left.buffer, 0, 4 * same))).toBe(true);
    expect(Buffer.from(held.right.buffer, 0, 4 * same).equals(Buffer.from(running.right.buffer, 0, 4 * same))).toBe(true);
  });

  test('held: the input is closed, the tape is not written, and every turn equals the first', async () => {
    await withRig({ frames: 65536, stores: true }, async (rig) => {
      const T = 2400;
      rig.set({ sync: true, syncTime: T, rpts: 0.5 });
      await rig.run(900, TONE);
      rig.set({ hold: true });
      // the tape takes WFADE more frames, then stands
      await rig.run(WFADE / 32 + 1, sine(1000, 0.25));
      expect((await rig.state(PS.HOLD))[0]).toBe(2);
      const before = await rig.tape();
      const take = await rig.run(75 * 60, sine(1000, 0.25));
      expect(rig.bankStores).toBe(0);
      expect(await rig.tape()).toEqual(before);
      // a 1 kHz input from the moment HOLD was taken: none of it in the loop
      expect(levelAt(take.left, 1000, T, 20 * T)).toBeLessThan(1e-4);
      // turn 20 against turn 55 (the DC blocker has settled): the same samples
      let worst = 0;
      for (let n = 0; n < T; n++) worst = Math.max(worst, Math.abs(take.left[20 * T + n]! - take.left[55 * T + n]!));
      expect(worst).toBeLessThan(1e-6);
      const turn = (k: number) => [rms(take.left, k * T, (k + 1) * T), levelAt(take.left, HZ, k * T, (k + 1) * T)];
      expect(turn(55)[0]! / turn(1)[0]!).toBeCloseTo(1, 4);
      expect(turn(55)[1]! / turn(1)[1]!).toBeCloseTo(1, 4);
      note(`HOLD, turn 55 against turn 1 of the loop: rms ${(turn(55)[0]! / turn(1)[0]!).toFixed(6)}, 220 Hz ${(turn(55)[1]! / turn(1)[1]!).toFixed(6)}; largest sample difference turn 20 to 55: ${worst.toExponential(1)}; stores to the tape in 60 s: ${rig.bankStores}`);
    });
  }, 300_000);

  test('held with COLR and HALO: they colour the output, and the turns still do not change', async () => {
    await withRig({ frames: 65536, stores: true }, async (rig) => {
      const T = 2400;
      rig.set({ sync: true, syncTime: T, rpts: 0.5, colr: -0.5, halo: 0.5 });
      await rig.run(900, sum(TONE, sine(3000, 0.1)));
      rig.set({ hold: true });
      await rig.run(WFADE / 32 + 1);
      const take = await rig.run(75 * 40);
      expect(rig.bankStores).toBe(0);
      const turn = (k: number) => [rms(take.left, k * T, (k + 1) * T), bandLevel(take.left, 2800, 3200, k * T, (k + 1) * T)];
      // the lines of HALO ring for a while after HOLD is taken: turn 8 on is settled
      expect(turn(38)[0]! / turn(8)[0]!).toBeCloseTo(1, 4);
      expect(turn(38)[1]! / turn(8)[1]!).toBeCloseTo(1, 4);
      expect(turn(38)[0]! / turn(1)[0]!).toBeCloseTo(1, 1);
      note(`HOLD with COLR -0.5 and HALO 0.5, turn 38 against turn 8: rms ${(turn(38)[0]! / turn(8)[0]!).toFixed(6)}, 3 kHz ${(turn(38)[1]! / turn(8)[1]!).toFixed(6)}; against turn 1: rms ${(turn(38)[0]! / turn(1)[0]!).toFixed(4)}`);
    });
  });

  test('held: ZONE, RATE and FLIP pick a part of the held sound and leave the tape as it is', async () => {
    await withRig({ frames: 65536, stores: true }, async (rig) => {
      const T = 4800;
      rig.set({ sync: true, syncTime: T, rpts: 0.5 });
      await rig.run(900, sum(TONE, noise(0.05)));
      rig.set({ hold: true });
      await rig.run(WFADE / 32 + 1);
      const before = await rig.tape();
      // let the loop come round to its start, then keep two whole turns
      await rig.run(900);
      const whole = await rig.run((2 * T) / 32);
      const period = async (controls: Partial<Controls>, frames: number, blocks = 300) => {
        rig.set(controls);
        // the crossfade to the new loop, and the DC blocker's settling
        await rig.run(600);
        const take = await rig.run(blocks);
        let worst = 0;
        for (let n = 0; n + frames < take.left.length; n++) worst = Math.max(worst, Math.abs(take.left[n]! - take.left[n + frames]!));
        expect(peak(take.left)).toBeGreaterThan(0.05);
        return worst;
      };
      // a shorter time, a zone and FLIP: the output repeats with exactly that time
      const zone2 = Math.round(zoneTime(2, 0.6));
      const periods = [
        await period({ syncTime: 1200 }, 1200),
        await period({ sync: false, zone: 2, rate: 0.6 }, zone2),
        await period({ flip: true }, zone2),
        await period({ flip: false, sync: true, syncTime: 9600 }, 9600, 700),
      ];
      note(`HOLD, largest difference between a sample and the one a loop later at 1200, ${zone2} (zone 2), ${zone2} reversed and 9600 frames: ${periods.map((v) => v.toExponential(1)).join(' ')}`);
      for (const worst of periods) expect(worst).toBeLessThan(1e-5);
      expect(rig.bankStores).toBe(0);
      expect(await rig.tape()).toEqual(before);
      // back at the first time the first loop is there again, sample for sample
      rig.set({ syncTime: T });
      await rig.run(600);
      const again = await rig.run((6 * T) / 32);
      let best = Infinity;
      for (let shift = 0; shift < T; shift++) {
        let worst = 0;
        for (let n = 0; n < T && worst < best; n += 7) worst = Math.max(worst, Math.abs(again.left[2 * T + shift + n]! - whole.left[n]!));
        best = Math.min(best, worst);
      }
      note(`HOLD, the first loop after the moves against before them: largest sample difference ${best.toExponential(1)}`);
      expect(best).toBeLessThan(1e-5);
    });
  });

  test('releasing HOLD records again: the write steps back and crossfades, and the next input is echoed', async () => {
    await withRig({ frames: 65536, stores: true }, async (rig) => {
      const T = 2400;
      rig.set({ sync: true, syncTime: T, rpts: 0 });
      await rig.run(900, TONE);
      rig.set({ hold: true });
      await rig.run(300, TONE);
      const [stood] = await rig.state(PS.WPOS);
      rig.set({ hold: false });
      await rig.run(1, TONE);
      const [resumed, , , , , fading] = await rig.state(PS.WPOS, 6);
      expect(resumed).toBe(stood! - WFADE + 32);
      expect(fading).toBe(WFADE - 32);
      expect(rig.bankStores).toBe(64);
      await rig.run(200);
      const at = rig.clock + 40;
      const origin = rig.clock;
      const take = await rig.run(100, impulse(at, 0.5));
      expect(peakAt(take.left) + origin - at).toBe(T);
    });
  });
});

describe('PRISM repeater engine: cost', () => {
  test('the heaviest block of every case here is the cost module.toml declares', async () => {
    await withRig({ frames: 65536 }, async (rig) => {
      // everything at once: both sides gliding with the drift, a zone change in the glide, HALO, the dark filter and the
      // limiter working; then the shortest reverse segments and held loops, and a release
      rig.set({ zone: 4, rate: 0.5, rpts: 1, colr: -1, halo: 1, skew: 0.3 });
      const input = sine(HZ, 0.9);
      await rig.run(900, input);
      for (let k = 0; k < 40; k++) {
        rig.set({ rate: 0.5 + 0.2 * Math.sin(k), skew: 0.3 + 0.2 * Math.cos(k) });
        await rig.run(10, input);
      }
      for (let k = 0; k < 20; k++) {
        rig.set({ zone: 3 + (k % 3), rate: 0.3 + 0.02 * k });
        await rig.run(7, input);
      }
      rig.set({ flip: true, zone: 1, rate: 0, skew: 1 });
      await rig.run(300, input);
      rig.set({ flip: false, hold: true });
      await rig.run(300, input);
      rig.set({ hold: false, zone: 4, rate: 0.5 });
      await rig.run(40, input);
      rig.set({ rate: 0.9 });
      await rig.run(400, input);
      // the heaviest: HOLD taken while the heads drift between frames and released before the loop turns. Both sides
      // then crossfade from a head between frames to a head that glides, HALO, the dark filter and the limiter work, and
      // the write crossfades into the tape
      for (let k = 0; k < 6; k++) {
        rig.set({ hold: true });
        await rig.run(17 + k, input);
        rig.set({ hold: false, rate: 0.9 - 0.01 * k });
        await rig.run(40, input);
      }
    });
    expect(PrismRig.worst).toBeLessThanOrEqual(DECLARED);
    // the declared cost is a measurement, not a ceiling picked with room: it is the heaviest block seen
    expect(PrismRig.worst).toBeGreaterThan(DECLARED * 0.95);
  });
});
