import { execFile } from 'node:child_process';
import path from 'node:path';
import { beforeAll, describe, expect, test } from 'vitest';

const bits = (value: number) => {
  const data = Buffer.alloc(4);
  data.writeFloatLE(value);
  return data.readUInt32LE();
};
type Command = {
  set?: Record<number, number>;
  storage?: Record<number, number>;
  action?: number;
  repeat?: number;
  samples?: number | number[];
  phase?: number;
  beat?: number;
  advance?: number;
  beatAdvance?: number;
  hostile?: boolean;
  audio?: boolean;
};
type Observation = {
  state: number[];
  status: number[];
  peak: number;
  finite: boolean;
  instructions: number;
  faults: number;
  diagnostics: unknown[];
  grains: number[][];
  audio?: number[][];
};
type Report = Record<string, { budget: number; steps: Observation[] }>;
const held: Command[] = [{}, { action: 1 }, { repeat: 4, audio: true }];
const restartChanges: { name: string; changed: Record<number, number> }[] = [
  { name: 'pattern', changed: { 12: 3, 13: 7 } },
  { name: 'missedEpoch', changed: { 12: 4, 13: 23 } },
  { name: 'selection', changed: { 10: 2, 11: 4, 12: 3, 13: 23 } },
  { name: 'recording', changed: { 12: 3, 13: 23 } },
  { name: 'invalidClock', changed: { 13: 12 } },
];
const cases: { name: string; commands: Command[] }[] = [
  { name: 'next', commands: [...held, { repeat: 3, samples: 0 }] },
  {
    name: 'past',
    commands: [
      { set: { 7: 1 }, samples: 0 },
      ...Array.from({ length: 21 }, (_, i) => ({ samples: (i + 1) / 64 })),
      { action: 1, audio: true },
    ],
  },
  { name: 'selection', commands: [...held, { action: 1, set: { 7: 1, 10: 2, 11: 4 } }] },
  { name: 'tempo', commands: [{}, { action: 1 }, { repeat: 4, advance: 8192 }, { advance: 16384 }, { audio: true }] },
  { name: 'pastEpoch', commands: [...held, { repeat: 8 }, { action: 1, set: { 7: 1, 12: 2 } }] },
  {
    name: 'cancel',
    commands: [...held, { action: 1, set: { 8: 8 } }, {}, { action: 2 }, { action: 1 }, { repeat: 17 }],
  },
  {
    name: 'invalidClear',
    commands: [
      ...held,
      { set: { 33: bits(0.8), 34: bits(0.99755), 35: bits(0.01), 36: bits(0.6) }, repeat: 20 },
      { action: 3, set: { 13: 6 }, samples: 0 },
      { repeat: 3, samples: 0 },
      { set: { 13: 7 }, repeat: 4, samples: 0 },
    ],
  },
  {
    name: 'invalidCancel',
    commands: [...held, { action: 1, set: { 9: 4 } }, { action: 2, set: { 13: 6 } }, { set: { 13: 7 }, repeat: 4 }],
  },
  {
    name: 'invalidArm',
    commands: [...held, { action: 1, set: { 13: 6 } }, { set: { 13: 7 }, repeat: 4 }],
  },
  {
    name: 'pastCapacity',
    commands: [...held, { repeat: 20 }, { action: 1, set: { 7: 1, 8: 8 } }],
  },
  { name: 'epoch', commands: [...held, { action: 1, set: { 9: 4 } }, { set: { 12: 2 } }] },
  {
    name: 'cue',
    commands: [
      { action: 1, set: { 9: 1 } },
      { repeat: 63 },
      {},
      { repeat: 4 },
      { action: 1, set: { 13: 13, 12: 2, 9: 0 } },
      { repeat: 4 },
    ],
  },
  ...[0, 1].map((from) => ({
    name: `stoppedCue${from}`,
    commands: [
      { set: { 7: from, 9: 2, 13: 13 }, advance: 0 },
      { action: 1, advance: 0 },
      { repeat: 3, advance: 0 },
      { set: { 12: 2, 13: 23 }, phase: 0, beat: 0 },
      { repeat: 3 },
      { audio: true },
      { repeat: 4, audio: true },
    ],
  })),
  ...restartChanges.map(({ name, changed }) => ({
    name: `restartCancel${name}`,
    commands: [
      ...held,
      { action: 1, set: { 9: name === 'recording' ? 0 : 4, 12: 2, 13: 13 }, advance: 0 },
      { set: changed, phase: 0, beat: 0 },
    ],
  })),
  {
    name: 'stoppedLaunch',
    commands: [...held, { beat: 64 * 65536 }, { set: { 12: 2, 13: 13 }, phase: 0, beat: 0, repeat: 12 }, { repeat: 4 }],
  },
  {
    name: 'handoff',
    commands: [...held, { action: 1, set: { 10: 2, 11: 4 } }, { repeat: 4, samples: 0.25 }, { repeat: 29 }, {}],
  },
  {
    name: 'fullHandoff',
    commands: [
      ...held,
      { set: { 23: 24000, 24: 1 }, repeat: 14 },
      { action: 1, set: { 24: 65536 } },
      { repeat: 4 },
      {},
      { repeat: 29 },
    ],
  },
  ...[0, 1, 2, 3].map((shape) => ({
    name: `shape${shape}`,
    commands: [{ set: { 26: shape } }, { action: 1 }, { repeat: 4 }, { repeat: 16 }],
  })),
  {
    name: 'crop',
    commands: [
      ...held,
      ...[
        [0.99, 1, 4, -2],
        [0, 0.001, 0.25, 2],
        [0.25, 0.5, 1, 0],
      ].map(([start, length, pitch, scan]) => ({
        set: { 21: bits(start!), 22: bits(length!), 25: bits(pitch!), 27: bits(1), 30: bits(scan!), 31: bits(1) },
        repeat: 5,
      })),
    ],
  },
  { name: 'off', commands: [...held, { repeat: 5 }, { set: { 24: 0 }, repeat: 10 }] },
  {
    name: 'space',
    commands: [
      ...held,
      { set: { 33: bits(0.8), 34: bits(0.99755), 35: bits(0.01), 36: bits(0.6) }, repeat: 20 },
      { set: { 24: 0 }, repeat: 10 },
      { action: 3, samples: 0 },
    ],
  },
  { name: 'limit', commands: [...held, { set: { 23: 24000, 24: 1 }, repeat: 14, hostile: true }] },
  {
    name: 'extreme',
    commands: [
      ...held,
      {
        set: {
          21: bits(0.99),
          22: bits(0.001),
          23: 24000,
          24: 2731,
          25: bits(4),
          26: 0,
          27: bits(1),
          28: bits(1),
          29: bits(1),
          30: bits(-2),
          31: bits(1),
          32: bits(1),
          33: bits(1),
          34: bits(0.99755),
          35: bits(0.95),
          36: bits(0.001),
          57: 36000,
        },
        repeat: 30,
        hostile: true,
      },
    ],
  },
  { name: 'badStorage', commands: [{ storage: { 3: 0x9aa2b9c8 } }] },
  { name: 'badLease', commands: [...held, { storage: { 2: 0x9a22b9c8 } }] },
  { name: 'badFloat', commands: [...held, { set: { 21: 0x7fc00001 } }] },
];

let report: Report;
beforeAll(async () => {
  const cwd = process.cwd();
  const child = execFile('uv', ['run', 'python', '-m', 'modules.fx.prism.tests.engine_probe'], {
    cwd,
    encoding: 'utf8',
    timeout: 600_000,
    maxBuffer: 8 * 1024 * 1024,
    env: { ...process.env, PYTHONPATH: [cwd, path.join(cwd, 'tests')].join(path.delimiter) },
  });
  const output = await new Promise<string>((resolve, reject) => {
    let stdout = '',
      stderr = '';
    child.stdout!.on('data', (chunk) => {
      stdout += String(chunk);
    });
    child.stderr!.on('data', (chunk) => {
      stderr += String(chunk);
    });
    child.once('error', reject);
    child.once('close', (code) => (code === 0 ? resolve(stdout) : reject(new Error(stderr || `probe exit ${code}`))));
    child.stdin!.end(JSON.stringify({ cases }));
  });
  report = JSON.parse(output) as Report;
  const measured = Object.entries(report)
    .map(([name, result]) => ({
      name,
      instructions: Math.max(...result.steps.map((step) => step.instructions)),
      budget: result.budget,
    }))
    .sort((a, b) => b.instructions - a.instructions);
  console.info('PRISM concrete-core worst blocks (including production FX guard):', measured.slice(0, 3));
}, 610_000);

const last = (name: string) => report[name]!.steps.at(-1)!;
const phrase = (step: Observation) => [step.state[11], step.state[12], step.state[13], step.state[14]];

describe('PRISM real SHARC engine', () => {
  test('preserves the production FX contract, state and leases in every scenario', () => {
    for (const [name, result] of Object.entries(report))
      for (const step of result.steps) {
        expect(step.diagnostics, name).toEqual([]);
        expect(step.finite, name).toBe(true);
        if (!name.startsWith('bad')) {
          expect(step.faults, name).toBe(0);
          expect(step.instructions, name).toBeLessThan(result.budget);
        }
      }
  });
  test('NEXT captures one musical interval and both stereo channels exactly', () => {
    const step = report.next!.steps[2]!;
    expect(step.state[13]).toBe(128);
    expect(step.state[14]).toBe(1);
    expect(step.status[20]).toBe(0);
    expect(step.status[24]).toBe(7);
    expect(step.status[31]).toBe(step.status[2]);
    expect(step.audio![0]).toEqual(Array(128).fill(0.625));
    expect(step.audio![1]).toEqual(Array(128).fill(-0.375));
    expect(phrase(last('next'))).toEqual(phrase(step));
  });
  test('PAST freezes the exact preceding ring window after physical wrap', () => {
    const step = last('past');
    expect(step.state[13]).toBe(128);
    const expected = [18, 19, 20, 21].flatMap((value) => Array(32).fill(value / 64));
    expect(step.audio![0]).toEqual(expected);
    expect(step.audio![1]).toEqual(expected);
  });
  test('new input selection invalidates history while preserving the held phrase', () => {
    expect(last('selection').status[4]).toBe(2);
    expect(phrase(last('selection'))).toEqual(phrase(report.selection!.steps[2]!));
  });
  test('NEXT follows musical phase through a tempo change instead of a fixed frame count', () => {
    const step = last('tempo');
    expect(step.status[11]).toBe(1);
    expect(step.state[13]).toBe(192);
    expect(step.audio![0]).toEqual(Array(192).fill(0.625));
    expect(step.audio![1]).toEqual(Array(192).fill(-0.375));
  });
  test('PAST never joins history across a transport discontinuity', () => {
    expect(last('pastEpoch').status[4]).toBe(2);
    expect(phrase(last('pastEpoch'))).toEqual(phrase(report.pastEpoch!.steps[2]!));
  });
  test('cancel, overflow and timeline discontinuity preserve the completed take', () => {
    const steps = report.cancel!.steps;
    expect(phrase(steps[5]!)).toEqual(phrase(steps[2]!));
    expect(phrase(last('cancel'))).toEqual(phrase(steps[2]!));
    expect(last('cancel').status[4]).toBe(4);
    expect(report.epoch!.steps[3]!.state[20]).toBe(3);
    expect(last('epoch').state[20]).toBe(0);
    expect(last('epoch').status[4]).toBe(3);
    expect(last('epoch').state[13]).toBe(128);
  });
  test('CLEAR immediately acknowledges and removes held readers and SPACE tails without a valid clock', () => {
    const steps = report.invalidClear!.steps;
    expect(steps[3]!.peak).toBeGreaterThan(0);
    for (const step of steps.slice(4)) {
      expect(step.status[17]).toBe(2);
      expect(step.status[20]).toBe(0);
      expect(step.state[13]).toBe(0);
      expect(step.state[14]).toBe(0);
      expect(step.state[18]).toBe(0);
      expect(step.state[38]).toBe(0);
      expect(step.peak).toBe(0);
    }
    expect(steps[4]!.state[10]).toBe(0);
    expect(steps[5]!.state[10]).toBe(0);
  });
  test('CANCEL acknowledges under an invalid clock and preserves the held phrase after clock recovery', () => {
    const steps = report.invalidCancel!.steps;
    expect(steps[3]!.status[20]).toBe(1);
    for (const step of steps.slice(4)) {
      expect(step.status[17]).toBe(3);
      expect(step.status[20]).toBe(0);
      expect(step.state[20]).toBe(0);
      expect(phrase(step)).toEqual(phrase(steps[2]!));
    }
    expect(steps[4]!.state[10]).toBe(0);
    expect(steps[4]!.state[28]).toBe(0);
  });
  test('ARM rejected by an invalid clock is acknowledged and cannot replay when timing recovers', () => {
    const steps = report.invalidArm!.steps;
    for (const step of steps.slice(3)) {
      expect(step.status[17]).toBe(2);
      expect(step.status[20]).toBe(0);
      expect(step.status[4]).toBe(3);
      expect(step.state[20]).toBe(0);
      expect(phrase(step)).toEqual(phrase(steps[2]!));
    }
    expect(steps[3]!.state[10]).toBe(0);
  });
  test('PAST reports TOO LONG when a full history bank cannot reach the requested start', () => {
    const steps = report.pastCapacity!.steps;
    expect(steps[3]!.state[10]).toBe(steps[3]!.state[33]);
    expect(last('pastCapacity').status[4]).toBe(4);
    expect(last('pastCapacity').status[17]).toBe(2);
    expect(last('pastCapacity').status[20]).toBe(0);
    expect(phrase(last('pastCapacity'))).toEqual(phrase(steps[2]!));
  });
  test('numbered CUE waits for its next boundary and NOW works while stopped', () => {
    const steps = report.cue!.steps;
    expect(steps[0]!.state[20]).toBe(3);
    expect(steps[1]!.state[20]).toBe(3);
    expect(steps[2]!.state[20]).toBe(4);
    expect(steps[3]!.state[13]).toBe(128);
    expect(last('cue').state[13]).toBe(128);
  });
  test.each([0, 1])('a stopped numbered CUE survives the qualified start for FROM %i', (from) => {
    const steps = report[`stoppedCue${from}`]!.steps;
    for (const index of [1, 2, 3, 4]) {
      expect(steps[index]!.state[20]).toBe(3);
      expect(steps[index]!.status[4]).toBe(0);
    }
    expect(steps[3]!.state[10], 'the old stopped history was discarded').toBe(1);
    expect(steps[5]!.state[20]).toBe(from === 0 ? 4 : 0);
    expect(steps[6]!.state[13]).toBe(128);
    expect(steps[6]!.status[20]).toBe(0);
    expect(steps[6]!.status[4]).toBe(0);
    expect(steps[6]!.audio![0]).toEqual(Array(128).fill(0.625));
    expect(steps[6]!.audio![1]).toEqual(Array(128).fill(-0.375));
  });
  test.each(['pattern', 'missedEpoch', 'selection', 'recording', 'invalidClock'])(
    'a %s discontinuity still cancels the pending take and preserves held audio',
    (reason) => {
      const steps = report[`restartCancel${reason}`]!.steps;
      expect(steps[3]!.state[20]).toBe(reason === 'recording' ? 4 : 3);
      expect(steps[4]!.state[20]).toBe(0);
      expect(steps[4]!.status[4]).toBe(3);
      expect(phrase(steps[4]!)).toEqual(phrase(steps[2]!));
    },
  );
  test('held grains resume promptly when a long-running beat clock resets on stop', () => {
    const steps = report.stoppedLaunch!.steps;
    expect(steps[3]!.state[45]).toBeGreaterThan(64 * 65536);
    expect(steps[4]!.state[45]).toBeLessThan(65536);
    expect(steps[5]!.state[38]).toBeGreaterThan(0);
    expect(steps[5]!.peak).toBeGreaterThan(0.01);
    expect(phrase(steps[5]!)).toEqual(phrase(steps[2]!));
  });
  test('old bank ownership lasts until all old readers retire', () => {
    const steps = report.handoff!.steps;
    const old = steps[2]!.state[11];
    expect(steps[4]!.state[11]).not.toBe(old);
    expect(steps[4]!.state[15]).toBe(old);
    expect(steps[4]!.state[14]).toBe(4);
    expect(steps[4]!.state[18]).toBe(1);
    expect(steps[4]!.state[8]).toBe(0);
    expect(steps[5]!.state[8]).toBe(0);
    const end = last('handoff');
    expect(end.state[8]).toBe(old);
    expect(end.state[18]).toBe(0);
    for (const grain of end.grains) expect(grain[0] === 0 || grain[6] !== old).toBe(true);
  });
  test('a saturated handoff starts the new phrase after one bounded old-reader fade', () => {
    const steps = report.fullHandoff!.steps;
    expect(steps[3]!.state[38]).toBe(8);
    expect(steps[5]!.status[3]).toBe(6);
    expect(steps[5]!.grains.some((grain) => grain[0]! > 0 && grain[12] === 0)).toBe(false);
    expect(steps[6]!.grains.some((grain) => grain[0]! > 0 && grain[12] === 0 && grain[6] === steps[6]!.state[11])).toBe(
      true,
    );
    for (const step of steps) expect(step.state[38]).toBeLessThanOrEqual(8);
    expect(last('fullHandoff').state[15]).toBe(0);
  });
  test.each([0, 1, 2, 3])('shape %i is audible and bounded', (shape) => {
    const step = last(`shape${shape}`);
    expect(step.peak).toBeGreaterThan(0.01);
    expect(step.peak).toBeLessThan(2);
  });
  test('extreme live crop, reverse, pitch and spray cannot expose unrecorded audio', () => {
    for (const step of report.crop!.steps.slice(3)) {
      expect(step.state[41]! + step.state[42]!).toBeLessThanOrEqual(step.state[13]!);
      expect(step.state[42]).toBeGreaterThanOrEqual(32);
      expect(step.faults).toBe(0);
    }
  });
  test('grain origin telemetry describes active readers inside the displayed crop', () => {
    for (const step of report.crop!.steps.slice(3)) {
      const low = (step.state[41]! / step.state[13]!) * 65535;
      const high = ((step.state[41]! + step.state[42]!) / step.state[13]!) * 65535;
      for (let slot = 0; slot < 8; slot++) {
        const valid = Boolean(step.status[29]! & (1 << slot));
        expect(valid).toBe(step.grains[slot]![0]! > 0 && step.grains[slot]![12] === 0);
        if (!valid) continue;
        const position = (step.status[25 + (slot >> 1)]! >>> ((slot & 1) * 16)) & 65535;
        expect(position).toBeGreaterThanOrEqual(low - 2);
        expect(position).toBeLessThanOrEqual(high + 2);
      }
    }
  });
  test('RATE OFF retires existing grains without erasing the phrase', () => {
    expect(last('off').state[38]).toBe(0);
    expect(last('off').state[13]).toBe(128);
  });
  test('SPACE sustains a bounded tail after grains stop, and CLEAR removes it', () => {
    const steps = report.space!.steps;
    expect(steps[4]!.state[38]).toBe(0);
    expect(steps[4]!.peak).toBeGreaterThan(0);
    expect(steps[4]!.peak).toBeLessThan(2);
    expect(last('space').state[13]).toBe(0);
    expect(last('space').peak).toBe(0);
  });
  test('voice exhaustion skips launches and stays under the declared instruction ceiling', () => {
    const step = last('limit');
    expect(step.state[38]).toBe(8);
    expect(step.status[16]).toBe(1);
    expect(step.instructions).toBeLessThan(report.limit!.budget);
  });
  test('combined control extremes stay bounded with all eight grain slots busy', () => {
    const step = last('extreme');
    expect(step.state[38]).toBe(8);
    expect(step.state[42]).toBe(32);
    expect(step.instructions).toBeLessThan(report.extreme!.budget);
  });
  test.each(['badStorage', 'badLease', 'badFloat'])('%s faults and releases held ownership', (name) => {
    const step = last(name);
    expect(step.faults).toBe(1);
    expect(step.status[5]).toBe(0);
    expect(step.status[8]).toBe(0);
  });
});
