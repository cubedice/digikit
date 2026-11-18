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
  /** The tape head's state words (prism.h PT_*, from PS_TAPE) and its output of the last block (32 left, 32 right). */
  tape: number[];
  head: number[];
  outputBits: number[];
  audio?: number[][];
};
type Report = Record<string, { budget: number; steps: Observation[] }>;
const held: Command[] = [{}, { action: 1 }, { repeat: 4, audio: true }];
// The packed tape word with the balance at "head only" (-511 in bits 22..31); add 1 for FLIP, 2 for HOLD, 4 * skew.
const HEAD_ONLY = 513 * 2 ** 22;
/** One block of a 3 kHz tone (16 frames a cycle), the same on both sides and continuous from block to block. */
const TONE = Array.from({ length: 64 }, (_, i) => 0.5 * Math.sin((2 * Math.PI * (i % 32)) / 16));
/** Block BLOCK of a ramp of 1/1024 a frame, the same on both sides. */
const RAMP = (block: number) => Array.from({ length: 64 }, (_, i) => (32 * block + (i % 32)) / 1024);
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
  // ---- the tape head. Word 58 TIME (frames), 59 FDBK, 60 the packed word (bit 0 FLIP, bit 1 HOLD, then SKEW, WOW and
  // BALANCE as signed ten-bit fields), 61 DRIVE. The test banks hold 512 frames (16 blocks).
  {
    name: 'tapeEcho',
    commands: [
      { set: { 58: 64, 60: HEAD_ONLY }, samples: 1 / 64 },
      ...Array.from({ length: 7 }, (_, i) => ({ samples: (i + 2) / 64 })),
    ],
  },
  {
    name: 'tapeFeedback',
    commands: [
      { set: { 58: 64, 59: bits(0.5), 60: HEAD_ONLY }, samples: TONE, repeat: 8 },
      ...Array.from({ length: 8 }, () => ({ samples: 0 })),
    ],
  },
  {
    name: 'tapeFeedbackCeiling',
    commands: [
      { set: { 58: 64, 59: bits(1.1), 60: HEAD_ONLY }, samples: TONE, repeat: 60 },
      { samples: 0, repeat: 4 },
    ],
  },
  {
    name: 'tapeNoFeedback',
    commands: [{ set: { 58: 64, 60: HEAD_ONLY }, samples: TONE, repeat: 8 }, { samples: 0, repeat: 3 }, { samples: 0 }],
  },
  {
    name: 'tapeHold',
    commands: [
      { set: { 58: 128, 60: HEAD_ONLY } },
      ...Array.from({ length: 9 }, (_, i) => ({ samples: (i + 2) / 64 })),
      { set: { 60: HEAD_ONLY + 2 }, samples: 0.9 },
      ...Array.from({ length: 6 }, () => ({ samples: 0.9 })),
    ],
  },
  {
    name: 'tapeReverse',
    commands: [
      { set: { 58: 64, 60: HEAD_ONLY + 1 }, samples: RAMP(0) },
      ...Array.from({ length: 11 }, (_, i) => ({ samples: RAMP(i + 1) })),
    ],
  },
  {
    name: 'tapeGlide',
    commands: [
      { set: { 58: 64, 60: HEAD_ONLY }, samples: TONE, repeat: 8 },
      { set: { 58: 96 }, samples: TONE, repeat: 8 },
      { samples: TONE, repeat: 300 },
    ],
  },
  {
    name: 'tapeSkew',
    commands: [{ set: { 58: 64, 60: HEAD_ONLY + 511 * 4 }, samples: TONE, repeat: 10 }],
  },
  {
    name: 'tapeReset',
    commands: [
      { set: { 58: 64, 60: HEAD_ONLY }, samples: TONE, repeat: 8 },
      { set: { 10: 2, 11: 4 }, samples: TONE },
      { samples: TONE },
      { samples: TONE, repeat: 6 },
    ],
  },
  {
    name: 'tapeDrive',
    commands: [{ set: { 58: 64, 60: HEAD_ONLY, 61: bits(1) }, samples: TONE, repeat: 10 }],
  },
  // Without a phrase the grains read the tape (the default packed word of the probe is "grains only").
  { name: 'tapeGrains', commands: [{ samples: TONE, repeat: 12 }, { samples: TONE, repeat: 40 }, { samples: TONE }] },
  {
    name: 'tapeGrainsReset',
    commands: [{ samples: TONE, repeat: 40 }, { set: { 10: 2, 11: 4 }, samples: TONE }, { samples: TONE }],
  },
  {
    name: 'tapeGrainsExtreme',
    commands: [
      { set: { 23: 24000, 25: bits(4), 27: bits(1), 29: bits(1), 31: bits(1) }, samples: TONE, repeat: 60 },
      { set: { 25: bits(0.25), 31: bits(0) }, samples: TONE, repeat: 60 },
      // short grains whose read just fits the window, backwards and forwards, from its far end
      { set: { 23: 240, 25: bits(1.5), 31: bits(1) }, samples: TONE, repeat: 60 },
      { set: { 31: bits(0) }, samples: TONE, repeat: 60 },
    ],
  },
  {
    name: 'tapeWithGrains',
    commands: [...held, { set: { 58: 64, 59: bits(1.1), 60: 3 }, repeat: 40 }, { set: { 60: 0 }, repeat: 40 }],
  },
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

  // ---- the tape head ----
  const headPeak = (step: Observation) => Math.max(...step.head.map(Math.abs));
  test('the head plays the tape TIME frames late, exactly, on both sides', () => {
    const steps = report.tapeEcho!.steps;
    // too little tape for the first blocks: silence, and a head that is not readable
    expect(steps[1]!.head).toEqual(Array(64).fill(0));
    expect(steps[1]!.tape[11]).toBe(0);
    // block 8 plays block 6 (64 frames are two blocks)
    expect(last('tapeEcho').head).toEqual(Array(64).fill(6 / 64));
    expect(last('tapeEcho').tape[11]).toBe(1);
    // the head alone is the effect's output (balance at the head, no drive, SPACE open)
    expect(last('tapeEcho').outputBits).toEqual(Array(64).fill(bits(6 / 64)));
    // no feedback: the tape advanced one block a call
    expect(last('tapeEcho').state[10]).toBe(8);
  });
  test('feedback writes the filtered head back onto the tape: repeats decay, and without feedback they end', () => {
    const steps = report.tapeFeedback!.steps;
    // after the input stops, each generation (two blocks) is quieter than the one before
    // step 0 is the eight blocks of tone; steps 1 to 8 are the silent blocks
    const generations = [2, 4, 6, 8].map((index) => headPeak(steps[index]!));
    for (let i = 1; i < generations.length; i++) {
      expect(generations[i]!).toBeGreaterThan(0.01);
      expect(generations[i]! / generations[i - 1]!).toBeGreaterThan(0.3);
      expect(generations[i]! / generations[i - 1]!).toBeLessThan(0.55);
    }
    expect(headPeak(last('tapeNoFeedback'))).toBe(0);
  });
  test('the feedback on the tape never exceeds half of full scale', () => {
    // the tone is 0.5: the tape holds the input and at most 0.5 of feedback, however long it repeats at 110 %
    const steps = report.tapeFeedbackCeiling!.steps;
    expect(headPeak(steps[0]!)).toBeGreaterThan(0.5);
    expect(headPeak(steps[0]!)).toBeLessThanOrEqual(1.0 + 1e-6);
    // the input is gone: what is left is feedback alone
    expect(headPeak(steps[1]!)).toBeLessThanOrEqual(0.5 + 1e-6);
  });
  test('feedback above 1 is held by the limiter', () => {
    for (const step of report.tapeWithGrains!.steps) {
      expect(step.finite).toBe(true);
      expect(step.peak).toBeLessThan(4);
    }
  });
  test('HOLD stops the tape and loops its last TIME frames', () => {
    const steps = report.tapeHold!.steps;
    const after = last('tapeHold');
    // the write position and the history stand still from the block after HOLD is seen
    expect(after.state[9]).toBe(steps[11]!.state[9]);
    expect(after.state[10]).toBe(steps[11]!.state[10]);
    expect(after.tape[10]).toBe(2);
    // the input after HOLD (0.9) never reaches the head: every sample is one of the tape's values, scaled by the
    // edge of the loop
    const peak = headPeak(after);
    expect(peak).toBeGreaterThan(0.05);
    expect(peak).toBeLessThanOrEqual(11 / 64 + 1e-6);
    // and the loop repeats: 128 frames are four blocks
    expect(steps[16]!.head).toEqual(steps[12]!.head);
  });
  test('FLIP plays each segment backwards', () => {
    const head = last('tapeReverse').head.slice(0, 32);
    expect(last('tapeReverse').tape[10]).toBe(1);
    // away from a segment edge, each sample is the frame before the last one: the ramp runs down by 1/1024
    const steps = head.slice(1).map((value, i) => head[i]! - value);
    expect(steps.filter((step) => Math.abs(step - 1 / 1024) < 1e-6).length).toBeGreaterThanOrEqual(8);
  });
  test('a change of TIME glides: the delay moves at most half a frame per sample and arrives', () => {
    const steps = report.tapeGlide!.steps;
    const delay = (step: Observation) => step.tape[0]! + new Float32Array(new Uint32Array([step.tape[1]!]).buffer)[0]!;
    expect(delay(steps[0]!)).toBe(64);
    const early = delay(steps[1]!);
    expect(early).toBeGreaterThan(64);
    expect(early).toBeLessThan(96);
    expect(early - 64).toBeLessThanOrEqual(8 * 32 * 0.5);
    expect(Math.abs(delay(last('tapeGlide')) - 96)).toBeLessThan(1);
    expect(headPeak(last('tapeGlide'))).toBeGreaterThan(0.3);
  });
  test('SKEW lengthens one side only', () => {
    const tape = last('tapeSkew').tape;
    expect(tape[0]).toBe(64);
    expect(tape[4]).toBeGreaterThanOrEqual(95);
    expect(tape[4]).toBeLessThanOrEqual(96);
  });
  test('a history reset runs the head to zero in one block and resumes when the tape is long enough again', () => {
    const steps = report.tapeReset!.steps;
    expect(headPeak(steps[0]!)).toBeGreaterThan(0.3);
    // the block after the reset: no readable tape, a ramp from the last sample to zero
    expect(steps[2]!.tape[11]).toBe(0);
    expect(Math.abs(steps[2]!.head[31]!)).toBeLessThan(1e-6);
    expect(headPeak(last('tapeReset'))).toBeGreaterThan(0.3);
    expect(last('tapeReset').tape[11]).toBe(1);
  });
  test('DRIVE saturates the output below 4/3', () => {
    const peak = last('tapeDrive').peak;
    expect(peak).toBeGreaterThan(0.9);
    expect(peak).toBeLessThanOrEqual(4 / 3 + 1e-6);
  });
  test('without a phrase the grains read windows of the tape', () => {
    const steps = report.tapeGrains!.steps;
    const active = (step: Observation) => step.grains.filter((grain) => grain[0]! > 0);
    expect(steps[0]!.state[13], 'no phrase is held').toBe(0);
    const sounding = [steps[1]!, steps[2]!].flatMap(active);
    expect(sounding.length).toBeGreaterThan(0);
    for (const grain of sounding) {
      // its bank is the rolling bank, its window lies in it and holds the grain's whole read
      expect(grain[6]).toBe(steps[1]!.state[8]);
      expect(grain[8]).toBeGreaterThanOrEqual(256);
      expect(grain[8]).toBeLessThanOrEqual(448);
      expect(grain[7]).toBeLessThan(512);
    }
    expect(Math.max(steps[1]!.peak, steps[2]!.peak)).toBeGreaterThan(0.01);
  });
  test('a history reset fades the tape grains out at once', () => {
    const steps = report.tapeGrainsReset!.steps;
    for (const grain of steps[1]!.grains) if (grain[8]) expect(grain[0]).toBeLessThanOrEqual(32);
    for (const grain of steps[2]!.grains) if (grain[8]) expect(grain[0]).toBeLessThanOrEqual(0);
  });
  test('tape grains at the extremes of size, pitch, spray and reverse stay inside written tape', () => {
    for (const step of report.tapeGrainsExtreme!.steps) {
      expect(step.finite).toBe(true);
      expect(step.diagnostics).toEqual([]);
    }
  });
});
