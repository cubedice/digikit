/**
 * PRISM's engine while it plays, pinned bit for bit on the SHARC-only rig: the output block, the wet mailbox and the tape of
 * five runs through every part of the block. The pins were taken from the engine of commit ee79d7018, before rest
 * (docs/findings/377) and the path-length changes that followed it, so a change that is meant to cost fewer instructions and
 * no more keeps them. The input sounds in every block, so the engine never rests here. A change that is meant to be heard
 * moves a pin: set it from the numbers of engine.rig.test.ts, not from the hash.
 */
import { createHash } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { afterAll, describe, expect, test } from 'vitest';
import { PS, PrismRig, noise, runSteps, sine, sum, type Controls, type Take } from './engine-rig.ts';

const measures: string[] = [];
afterAll(() => {
  const report = ['PRISM active path:', ...measures].join('\n  ');
  console.info(report);
  mkdirSync('out/runs/prism-engine', { recursive: true });
  writeFileSync('out/runs/prism-engine/active-pins.txt', `${report}\n`);
});

/** The take's four signals and the tape (a word nothing wrote counts as one fixed word). */
async function digest(rig: PrismRig, takes: Take[]): Promise<string> {
  const hash = createHash('sha256');
  for (const take of takes)
    for (const x of [take.left, take.right, take.wetLeft, take.wetRight]) hash.update(new Uint8Array(x.buffer, x.byteOffset, x.byteLength));
  const tape = Uint32Array.from(await rig.tape(), (w) => w ?? 0xdeadbeef);
  hash.update(new Uint8Array(tape.buffer));
  return hash.digest('hex');
}

interface Run {
  name: string;
  frames: number;
  pin: string;
  body: (rig: PrismRig) => Promise<Take[]>;
}

const steps = async (rig: PrismRig, input: Parameters<typeof runSteps>[1], list: [number, Partial<Controls>][]): Promise<Take[]> => [
  (await runSteps(rig, input, list)).take,
];

const RUNS: Run[] = [
  {
    // every mode of the heads, both ends of COLR, HALO and its late network coming and going
    name: 'modes',
    frames: 16384,
    pin: '19c9646409229dbe553b1c8858870fa0ede2dbead254849213c70db2d58ad711',
    body: (rig) =>
      steps(rig, sine(220, 0.08), [
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
        [60, { halo: 0.3, mix: 0.4, rpts: 0 }],
        [60, { halo: 0.8, mix: 1, rpts: 0.75, sync: false, zone: 2, rate: 1 }],
      ]),
  },
  {
    // the limiter above its knee, the glide with the drift, crossfades between frames, releases
    name: 'heavy',
    frames: 65536,
    pin: 'a9cf193fce358276fb52edd95a3071f351c930e04bbf91f0195785f59f7cbfba',
    body: async (rig) => {
      const input = sine(220, 0.9);
      const list: [number, Partial<Controls>][] = [[300, { zone: 4, rate: 0.5, rpts: 1, colr: -1, halo: 1, skew: 0.3 }]];
      for (let k = 0; k < 12; k++) list.push([10, { rate: 0.5 + 0.2 * Math.sin(k), skew: 0.3 + 0.2 * Math.cos(k) }]);
      for (let k = 0; k < 8; k++) list.push([7, { zone: 3 + (k % 3), rate: 0.3 + 0.02 * k }]);
      list.push([120, { flip: true, zone: 1, rate: 0, skew: 1 }], [120, { flip: false, hold: true }], [40, { hold: false, zone: 4, rate: 0.5 }]);
      for (let k = 0; k < 3; k++) list.push([17 + k, { hold: true }], [40, { hold: false, rate: 0.9 - 0.01 * k }]);
      return steps(rig, input, list);
    },
  },
  {
    // the loop effect: its tap as the input, the output in its mailbox, both switches moving, ping pong and swap
    name: 'loop',
    frames: 16384,
    pin: 'b145d473eb2d8929263aa5a38913107543e82ce8b70eb674ef142c2785f9786a',
    body: async (rig) => {
      const input = sum(sine(220, 0.2), noise(0.05, 7));
      const tap = sine(330, 0.3);
      const takes: Take[] = [];
      rig.set({ sync: true, syncTime: 960, rpts: 0.6, halo: 0.4 });
      takes.push(await rig.run(60, input));
      rig.loop = { alive: true, tap };
      takes.push(await rig.run(40, input));
      rig.set({ input: 1 });
      takes.push(await rig.run(40, input));
      rig.set({ pingPong: true });
      takes.push(await rig.run(40, input));
      rig.loop = { alive: true, tap, count: 1 };
      rig.set({ input: 0, skew: 0.4 });
      takes.push(await rig.run(40, input));
      rig.set({ pingPong: false, swap: true, skew: 0 });
      takes.push(await rig.run(40, input));
      rig.loop = undefined;
      takes.push(await rig.run(30, input));
      rig.loop = { alive: true, tap, audible: false };
      rig.set({ input: 1, swap: false });
      takes.push(await rig.run(30, input));
      return takes;
    },
  },
  {
    // a random walk through the controls from an empty tape
    name: 'walk',
    frames: 8192,
    pin: '27fa0a53c7f4d84cfd6a755f5393015c7a78673c4837917f76bb0a524c78bf29',
    body: async (rig) => {
      let state = 0x9e3779b1;
      const random = () => {
        state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
        return state / 2 ** 32;
      };
      const pick = <T>(values: readonly T[]): T => values[Math.floor(random() * values.length)]!;
      const takes: Take[] = [];
      for (let step = 0; step < 140; step++) {
        const change: Partial<Controls> = {};
        const turns = 1 + Math.floor(random() * 3);
        for (let k = 0; k < turns; k++) {
          const which = Math.floor(random() * 12);
          if (which === 0) change.zone = 1 + Math.floor(random() * 8);
          else if (which === 1) change.rate = pick([0, 1, random()]);
          else if (which === 2) change.sync = random() < 0.5;
          else if (which === 3) change.syncTime = pick([1, 32, 33, 64, 97, 480, 2400, 4000, 4096, 100000]);
          else if (which === 4) change.flip = random() < 0.5;
          else if (which === 5) change.hold = random() < 0.5;
          else if (which === 6) change.skew = pick([-1, 0, 1, 2 * random() - 1]);
          else if (which === 7) change.rpts = pick([0, 0.75, 1, random()]);
          else if (which === 8) change.colr = pick([-1, 0, 0.5, 1, 2 * random() - 1]);
          else if (which === 9) change.halo = pick([0, 1, random()]);
          else if (which === 10) change.mix = pick([0, 1, random()]);
          else change.pingPong = random() < 0.5;
        }
        rig.set(change);
        takes.push(await rig.run(pick([1, 1, 2, 3, 7, 16, 40]), sum(sine(220, 0.4), noise(0.1, 100 + step))));
      }
      return takes;
    },
  },
  {
    // single samples of both signs with silence between them: long runs of zeros and of words near them in the wet signal,
    // at the three kinds of colour, with HALO, into the loop effect, and held
    name: 'sparse',
    frames: 8192,
    pin: '02bd56fe97fa28db868e3b0e1f07f84d4daf8842f345baa2951418e4ac358964',
    body: async (rig) => {
      const input = (n: number): number => (n % 1777 === 5 ? (Math.floor(n / 1777) % 2 ? -0.4 : 0.3) : 0);
      const takes: Take[] = [];
      rig.set({ sync: true, syncTime: 480, rpts: 0.5 });
      takes.push(await rig.run(150, input));
      rig.set({ colr: 1 });
      takes.push(await rig.run(100, input));
      rig.set({ colr: -1, halo: 0.6 });
      takes.push(await rig.run(100, input));
      rig.loop = { alive: true, tap: () => 0, audible: false };
      rig.set({ colr: 0.5 });
      takes.push(await rig.run(150, input));
      rig.set({ halo: 0, rpts: 0 });
      takes.push(await rig.run(100, input));
      rig.set({ hold: true, syncTime: 2400 });
      takes.push(await rig.run(200, input));
      rig.set({ hold: false, flip: true });
      takes.push(await rig.run(100, input));
      return takes;
    },
  },
];

describe('PRISM repeater engine: the active path is pinned', () => {
  test.each(RUNS)('$name: the output, the mailbox and the tape are the pinned words', async (run) => {
    const rig = await PrismRig.open({ frames: run.frames, poison: true });
    try {
      const takes = await run.body(rig);
      const got = await digest(rig, takes);
      const counts = takes.flatMap((t) => t.instructions);
      const blocks = counts.length;
      measures.push(
        `${run.name}: ${got}; ${blocks} blocks, instructions a block: mean ${(counts.reduce((a, b) => a + b, 0) / blocks).toFixed(0)}, most ${Math.max(...counts)}, least ${Math.min(...counts)}`,
      );
      // the engine played in every block
      expect((await rig.state(PS.IDLE))[0]).toBe(0);
      expect(got).toBe(run.pin);
    } finally {
      await rig.close();
    }
  });

  // For the record of a change of the path's length (out/runs/prism-engine/active-pins.txt): a block that plays a steady
  // tone with every control at rest, core/fx included.
  test('the cost of a steady block, without and with HALO, on the FX return and into the loop effect', async () => {
    const tone = sine(220, 0.08);
    const steady = async (controls: Partial<Controls>, loop: boolean): Promise<number> => {
      const rig = await PrismRig.open({ frames: 16384 });
      try {
        if (loop) rig.loop = { alive: true, tap: () => 0, audible: false };
        rig.set({ sync: true, syncTime: 2400, rpts: 0.5, ...controls });
        await rig.run(600, tone);
        // the most of 20 blocks; with FLIP the mean of 300 (a reverse segment of 2400 samples crossfades for 960 of them)
        const counts = (await rig.run(controls.flip ? 300 : 20, tone)).instructions;
        return controls.flip ? Math.round(counts.reduce((a, b) => a + b, 0) / counts.length) : Math.max(...counts);
      } finally {
        await rig.close();
      }
    };
    const rows: string[] = [];
    for (const [name, controls] of [
      ['HALO off', {}],
      ['HALO 0.5', { halo: 0.5 }],
      ['HALO off, COLR 0', { colr: 0 }],
      ['HALO off, FLIP', { flip: true }],
    ] as const) {
      const [alone, withLoop] = [await steady(controls, false), await steady(controls, true)];
      expect(alone).toBeGreaterThan(1000);
      rows.push(`${name} ${alone} (into the loop effect ${withLoop})`);
    }
    measures.push(`a steady block: ${rows.join('; ')} instructions`);
  });
});
