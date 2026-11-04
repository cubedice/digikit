import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

let directory: string, binary: string;
beforeAll(() => {
  directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-movement-'));
  binary = path.join(directory, 'movement');
  execFileSync(
    'cc',
    [
      '-std=c11',
      '-Wall',
      '-Wextra',
      '-Werror',
      '-fsanitize=undefined',
      path.join(import.meta.dirname, 'canopy-movement.c'),
      path.resolve(import.meta.dirname, '../../../modules/machines/canopy/src/movement.c'),
      '-o',
      binary,
    ],
    { timeout: 20_000 },
  );
});
afterAll(() => {
  if (directory) fs.rmSync(directory, { recursive: true, force: true });
});
interface Row {
  block: number;
  result: number;
  status: number;
  ordinal: number;
  phase: number;
  events: number[][];
}
function run(commands: string): Row[] {
  return execFileSync(binary, { input: commands + '\nstate\n', encoding: 'utf8', timeout: 5_000 })
    .trim()
    .split('\n')
    .map((line) => JSON.parse(line) as Row);
}
function config(
  options: Partial<
    Record<'clock' | 'rate' | 'order' | 'octaves' | 'gate' | 'swing' | 'restart' | 'bpm' | 'strum' | 'mix', number>
  > = {},
) {
  const c = {
    clock: 1,
    rate: 4,
    order: 0,
    octaves: 1,
    gate: 50,
    swing: 0,
    restart: 2,
    bpm: 120,
    strum: 0,
    mix: 127,
    ...options,
  };
  return `config ${Object.values(c).join(' ')}\n`;
}
function chord(notes = [60, 64, 67], revision = 1, phrase = 1, generated = 0) {
  return `chord ${revision} ${phrase} ${generated} ${notes.length} ${notes.map((n) => `${n + 1} ${n} 100 0 0`).join(' ')}\n`;
}
const onsets = (rows: Row[], lane = 1) =>
  rows.flatMap((r) =>
    r.events.filter((e) => e[0] === 1 && e[1] === lane).map((e) => ({ block: r.block, token: e[2], note: e[3] })),
  );

test.each([
  [0, [60, 64, 67, 60, 64]],
  [1, [67, 64, 60, 67, 64]],
  [2, [60, 64, 67, 64, 60]],
  [3, [67, 60, 64, 67, 60]],
] as const)('order %i uses admitted membership', (order, expected) => {
  const rows = run(config({ order }) + chord([67, 60, 64]) + 'tick 751');
  expect(onsets(rows).map((e) => e.note)).toEqual(expected);
});
test('one-note UP/DOWN and clipped octave expansion remain playable', () => {
  expect(onsets(run(config({ order: 2 }) + chord([60]) + 'tick 751')).map((e) => e.note)).toEqual([60, 60, 60, 60, 60]);
  expect(onsets(run(config({ octaves: 3 }) + chord([115]) + 'tick 751')).map((e) => e.note)).toEqual([
    115, 127, 115, 127, 115,
  ]);
});
test('three octaves and seeded random stay bounded and repeatable', () => {
  const cmd = config({ order: 4, octaves: 3 }) + chord() + 'tick 10000';
  const a = onsets(run(cmd)),
    b = onsets(run(cmd));
  expect(a).toEqual(b);
  const pitches = new Set(a.map((e) => e.note));
  expect(pitches.size).toBe(9);
  expect([...pitches].sort((x, y) => x! - y!)).toEqual([60, 64, 67, 72, 76, 79, 84, 88, 91]);
  expect(new Set(a.map((e) => e.token)).size).toBe(a.length);
});
test('triplets retain rational remainder over a long run', () => {
  const notes = onsets(run(config({ rate: 3, bpm: 137 }) + chord() + 'tick 200000'));
  for (let i = 0; i < notes.length; i++) expect(notes[i]!.block).toBe(Math.ceil((i * 90000) / (137 * 3)));
});
test('swing preserves pair duration rather than accumulating rounded intervals', () => {
  const notes = onsets(run(config({ rate: 3, bpm: 137, swing: 63 }) + chord() + 'tick 200000'));
  for (let i = 0; i < notes.length; i += 2) expect(notes[i]!.block).toBe(Math.ceil((i * 90000) / (137 * 3)));
  expect(notes[1]!.block).not.toBe(Math.ceil(90000 / (137 * 3)));
});
test('a chord edit commits at the next arp onset without releasing keyboard pads', () => {
  const rows = run(config() + chord() + 'tick 100\n' + chord([60, 65, 69], 2) + 'tick 100');
  expect(onsets(rows)).toEqual([
    { block: 0, token: 1, note: 60 },
    { block: 188, token: 2, note: 65 },
  ]);
  const changes = rows.find((r) => r.block === 100)!.events;
  expect(changes.filter((e) => e[1] === 0 && e[3] === 60)).toEqual([]);
});
test('TRIG restarts only on a new phrase; CONTINUE preserves ordinal on edits', () => {
  const rows = run(
    config({ restart: 0 }) +
      chord([60]) +
      'tick 100\n' +
      chord([60, 64], 2) +
      'tick 100\n' +
      chord([60, 64], 3, 2) +
      'tick 1',
  );
  expect(onsets(rows).map((e) => [e.block, e.note])).toEqual([
    [0, 60],
    [188, 64],
    [200, 60],
  ]);
});
test('PATTERN restarts on committed identity and transport reset always restarts', () => {
  const rows = run(config({ restart: 1 }) + chord() + 'tick 200\nclock 1 1 1 1 2\ntick 1\nclock 1 1 2 1 2\ntick 1');
  expect(onsets(rows).map((e) => [e.block, e.note])).toEqual([
    [0, 60],
    [188, 64],
    [200, 60],
    [201, 60],
  ]);
});
test('generated strums are bounded, pending notes reschedule and obsolete notes cancel', () => {
  const rows = run(
    config({ mix: 0, strum: 300 }) +
      chord([60, 64, 67], 1, 1, 1) +
      'tick 100\n' +
      chord([60, 64, 69], 2, 1, 1) +
      'tick 401',
  );
  expect(onsets(rows, 0).map((e) => [e.block, e.note])).toEqual([
    [0, 60],
    [250, 64],
    [400, 69],
  ]);
});
test('PLAY ignores strum and an added note leaves the first held note alone', () => {
  const rows = run(config({ mix: 0, strum: 750 }) + chord([60]) + 'tick 1\n' + chord([60, 64], 2) + 'tick 1');
  expect(onsets(rows, 0).map((e) => [e.block, e.note])).toEqual([
    [0, 60],
    [1, 64],
  ]);
  expect(rows.flatMap((r) => r.events).filter((e) => e[0] === 0)).toEqual([]);
});
test('STOP cancels pending strums and synced arp while held pad remains', () => {
  const rows = run(
    config({ clock: 0, rate: 1, strum: 300 }) +
      'clock 1 1 1 1 1\n' +
      chord([60, 64, 67], 1, 1, 1) +
      'tick 50\nclock 1 0 1 1 1\ntick 400',
  );
  expect(onsets(rows, 0).map((e) => e.note)).toEqual([60]);
  expect(onsets(rows)).toHaveLength(1);
  expect(rows.at(-1)!.status).toBe(0);
  expect(rows.flatMap((r) => r.events).filter((e) => e[0] === 0 && e[1] === 0)).toEqual([]);
});
test('master clock only triggers on committed boundaries and integer multiples', () => {
  const rows = run(
    config({ clock: 0, rate: 2 }) +
      chord() +
      'clock 1 1 1 1 1\ntick 188\nclock 1 1 1 2 1\ntick 187\nclock 1 1 1 3 1\ntick 1',
  );
  expect(onsets(rows).map((e) => [e.block, e.note])).toEqual([
    [0, 60],
    [375, 64],
  ]);
});
test('clock loss has no local fallback; a new boundary resumes', () => {
  const rows = run(
    config({ clock: 0, rate: 1 }) + chord() + 'clock 1 1 1 1 1\ntick 12002\nstate\nclock 1 1 1 2 1\ntick 1',
  );
  expect(onsets(rows).map((e) => e.block)).toEqual([0, 12002]);
  expect(rows.find((r) => r.block === 12002 && r.result === 0)!.status).toBe(3);
});
test('invalid/missed clock frames pause, never emit catch-up bursts', () => {
  const rows = run(
    config({ clock: 0, rate: 1 }) +
      chord() +
      'clock 1 1 1 1 1\ntick 10\nclock 1 1 1 4 1\ntick 10\nclock 0 1 1 5 1\ntick 10\nclock 1 1 1 6 1\ntick 1',
  );
  expect(onsets(rows).map((e) => e.block)).toEqual([0, 30]);
});
test('gate releases are confined to arp ownership, including a full-gate replacement', () => {
  const rows = run(config({ gate: 100 }) + chord() + 'tick 751');
  const offs = rows.flatMap((r) => r.events).filter((e) => e[0] === 0);
  expect(offs).toHaveLength(4);
  expect(offs.every((e) => e[1] === 1)).toBe(true);
  for (const r of rows) if (r.events.some((e) => e[0] === 0)) expect(r.events[0]![0]).toBe(0);
});
test('tempo changes preserve phase while changing future spacing', () => {
  const rows = run(config() + chord() + 'tick 100\n' + config({ bpm: 240 }) + 'tick 300');
  expect(onsets(rows).map((e) => e.block)).toEqual([0, 144, 237, 331]);
});
test('invalid configuration is atomic and invalid block sequence is rejected', () => {
  const rows = run(config() + chord() + 'tick 100\n' + config({ rate: 5 }) + 'tick 100\nblock 500\ntick 1');
  expect(onsets(rows).map((e) => e.block)).toEqual([0, 188]);
  expect(rows.filter((r) => r.result === -1)).toHaveLength(2);
});
test('PANIC clears both lanes and all pending strums', () => {
  const rows = run(
    config({ strum: 300 }) + chord([60, 64, 67], 1, 1, 1) + 'tick 10\npanic\n' + chord([], 2, 2) + 'tick 1000',
  );
  expect(onsets(rows)).toHaveLength(1);
  expect(onsets(rows, 0)).toHaveLength(1);
  expect(rows.find((r) => r.block === 10 && r.result > 0)!.events.map((e) => [e[0], e[1]])).toEqual([
    [0, 1],
    [0, 0],
  ]);
});
test('token exhaustion stops arp admission instead of recycling active identity', () => {
  const rows = run(config() + chord() + 'token 4294967295\ntick 400');
  expect(onsets(rows).map((e) => e.token)).toEqual([4294967295]);
  expect(rows.at(-1)!.ordinal).toBe(1);
});
test('full chord replacement plus arp transition fits the bounded output buffer', () => {
  const rows = run(
    config({ gate: 100, bpm: 150 }) + chord([48, 52, 55, 59]) + 'tick 150\n' + chord([60, 64, 67, 71], 2) + 'tick 1',
  );
  expect(rows.find((r) => r.block === 150)!.result).toBe(10);
  expect(rows.find((r) => r.block === 150)!.events.map((e) => e[0])).toEqual([0, 0, 0, 0, 0, 1, 1, 1, 1, 1]);
});
test('unsigned audio-block wrap preserves pending strum deadlines', () => {
  const rows = run(config({ mix: 0, strum: 30 }) + 'block 4294967280\n' + chord([60, 64], 1, 1, 1) + 'tick 40');
  expect(onsets(rows, 0).map((e) => e.block)).toEqual([4294967280, 14]);
});

test('live swing changes wait for a complete pair', () => {
  const rows = run(
    config({ bpm: 150, swing: 50 }) + chord() + 'tick 200\n' + config({ bpm: 150, swing: 0 }) + 'tick 401',
  );
  expect(onsets(rows).map((e) => e.block)).toEqual([0, 225, 300, 450, 600]);
});
test('turning arp off releases its gate without stopping any pad', () => {
  const rows = run(config({ gate: 100 }) + chord() + 'tick 20\n' + config({ mix: 0 }) + 'tick 500');
  const changes = rows.find((r) => r.block === 20)!.events;
  expect(changes.map((e) => [e[0], e[1]])).toEqual([[0, 1]]);
  expect(onsets(rows)).toHaveLength(1);
});
test('switching to master waits for the selected clock instead of continuing local timing', () => {
  const rows = run(
    config() + chord() + 'tick 20\n' + config({ clock: 0, rate: 1 }) + 'tick 500\nclock 1 1 1 1 1\ntick 1',
  );
  expect(onsets(rows).map((e) => e.block)).toEqual([0, 520]);
});
test('master clock fault remains visible until a fresh boundary', () => {
  const rows = run(
    config({ clock: 0, rate: 1 }) + chord() + 'clock 1 1 1 1 1\ntick 10\nclock 1 1 1 4 1\ntick 100\nstate',
  );
  expect(rows.at(-1)!.status).toBe(3);
  expect(onsets(rows)).toHaveLength(1);
});
test('changed membership with a reused revision is rejected before any musical mutation', () => {
  const rows = run(config() + chord() + 'tick 1\n' + chord([60, 65, 69]) + 'tick 1\n' + chord() + 'tick 188');
  expect(rows.filter((r) => r.result < 0)).toHaveLength(1);
  expect(onsets(rows).map((e) => e.note)).toEqual([60, 64]);
});
test('arp copies velocity and route from the admitted candidate without reusing its token', () => {
  const rows = run(config() + 'chord 1 1 0 1 1234 127 20 1 15\ntick 1');
  expect(rows[0]!.events).toEqual([
    [1, 0, 1234, 127, 20, 1, 15],
    [1, 1, 1, 127, 20, 1, 15],
  ]);
});
