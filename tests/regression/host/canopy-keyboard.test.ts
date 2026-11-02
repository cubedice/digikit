import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

let directory: string, binary: string, settingsBinary: string;
beforeAll(() => {
  directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-keyboard-'));
  binary = path.join(directory, 'keyboard');
  const root = path.resolve(import.meta.dirname, '../../..');
  execFileSync(
    'cc',
    [
      '-std=c11',
      '-Wall',
      '-Wextra',
      '-Werror',
      '-fsanitize=undefined',
      path.join(import.meta.dirname, 'canopy-keyboard.c'),
      ...['instrument-play/src/cf/keyboard.c', 'midi-events/src/cf/ownership.c', 'midi-events/src/cf/transport.c'].map(
        (f) => path.join(root, 'modules/core/adapters', f),
      ),
      '-o',
      binary,
    ],
    { timeout: 20_000 },
  );
  settingsBinary = path.join(directory, 'settings');
  execFileSync(
    'cc',
    [
      '-std=c11',
      '-Wall',
      '-Wextra',
      '-Werror',
      '-fsanitize=undefined',
      '-I',
      path.join(root, 'sdk/include'),
      path.join(import.meta.dirname, 'canopy-settings.c'),
      path.join(root, 'modules/core/adapters/instrument-play/src/cf/keyboard.c'),
      path.join(root, 'modules/core/adapters/instrument-play/src/cf/preset.c'),
      ...['src-state.c', 'status.c', 'src-display.c'].map((f) =>
        path.join(root, 'modules/core/adapters/instrument-play/src/cf', f),
      ),
      '-o',
      settingsBinary,
    ],
    { timeout: 20_000 },
  );
});
afterAll(() => {
  if (directory) fs.rmSync(directory, { recursive: true, force: true });
});
interface State {
  result: number;
  mode: number;
  status: number;
  type: number;
  revision: number;
  session: number;
  queued: number;
  reset: number;
  voices: number[][];
  capture: number[];
  shape: number[];
  packet: number[];
}
function run(commands: string): State[] {
  return execFileSync(binary, { input: commands + '\n', encoding: 'utf8', timeout: 2_000 })
    .trim()
    .split('\n')
    .map((line) => JSON.parse(line) as State);
}
const notes = (s: State) => s.voices.map((v) => v[0]).sort((a, b) => a! - b!);
test('PLAY preserves exact token, velocity and phase identity of untouched notes', () => {
  const [a, b, c] = run('on 0 0 60 40\non 0 0 64 90\ntick\non 0 0 67 127\ntick\noff 0 0 64 0\ntick');
  expect(notes(a!)).toEqual([60, 64]);
  expect(notes(c!)).toEqual([60, 67]);
  expect(c!.voices.find((v) => v[0] === 60)).toEqual(a!.voices[0]);
  expect(c!.voices.find((v) => v[0] === 67)).toEqual(b!.voices.find((v) => v[0] === 67));
});
test('fifth note steals oldest; its stale release never releases replacement or resurrects stolen keys', () => {
  const [a, b, c] = run(
    'on 0 0 60 90\non 0 0 62 90\non 0 0 64 90\non 0 0 65 90\ntick\non 0 0 67 90\ntick\noff 0 0 60 0\ntick',
  );
  expect(notes(a!)).toEqual([60, 62, 64, 65]);
  expect(notes(b!)).toEqual([62, 64, 65, 67]);
  expect(c!.voices).toEqual(b!.voices);
});
test('repeated same pitch is released FIFO on both ports and channels 1 and 16', () => {
  for (const port of [0, 1])
    for (const ch of [0, 15]) {
      const [a, b] = run(`on ${ch} ${port} 60 40\non ${ch} ${port} 60 127\ntick\noff ${ch} ${port} 60 0\ntick`);
      expect(a!.voices).toHaveLength(2);
      expect(b!.voices).toEqual([a!.voices[1]]);
    }
});
test('reset tombstone absorbs old C-off after fresh C-on', () => {
  const [a, b, c] = run('on 0 0 60 90\ntick\nreset\non 0 0 60 110\ntick\noff 0 0 60 0\ntick\noff 0 0 60 0\ntick');
  expect(notes(a!)).toEqual([60]);
  expect(c!.voices).toEqual(b!.voices);
  const states = run('on 0 0 60 90\nreset\non 0 0 60 110\noff 0 0 60 0\noff 0 0 60 0\ntick');
  expect(states[0]!.voices).toEqual([]);
});
test('pedal, latch replacement and latch off retain only current ownership', () => {
  const states = run(
    'latch 1\non 0 0 60 90\non 0 0 64 90\ntick\ncc 0 0 64 127\ntick\noff 0 0 60 0\noff 0 0 64 0\ntick\non 0 0 62 100\ntick\non 0 0 65 100\ntick\nlatch 0\ntick\noff 0 0 62 0\noff 0 0 65 0\ntick\ncc 0 0 64 0\ntick',
  );
  expect(notes(states[2]!)).toEqual([60, 64]);
  expect(notes(states[3]!)).toEqual([62]);
  expect(notes(states[5]!)).toEqual([62, 65]);
  expect(notes(states[6]!)).toEqual([62, 65]);
  expect(notes(states[7]!)).toEqual([]);
});
test('same-batch selectors run before roots and remain silent, with last-root priority', () => {
  const states = run(
    'mode 1\non 0 0 60 100\non 0 0 28 127\ntick\non 0 0 62 80\ntick\noff 0 0 62 0\ntick\noff 0 0 28 0\ntick',
  );
  expect(notes(states[0]!)).toEqual([60, 63, 67]);
  expect(notes(states[1]!)).toEqual([62, 65, 69]);
  expect(notes(states[2]!)).toEqual([60, 63, 67]);
  expect(states[3]!.voices).toEqual(states[2]!.voices);
});
test('separate selector channel is silent and consumes selector-only packets locally', () => {
  const states = run('mode 1\nselector 15\non 15 1 64 127\ntick\non 0 0 60 90\ntick');
  expect(states[0]!.packet).toEqual([]);
  expect(states[0]!.queued).toBe(0);
  expect(notes(states[1]!)).toEqual([60, 63, 67]);
});
test('selector across packet boundary changes only subsequent resolved voicing', () => {
  const states = run('mode 1\non 0 0 60 90\ntick\non 0 0 28 127\ntick');
  expect(notes(states[0]!)).toEqual([60, 64, 67]);
  expect(notes(states[1]!)).toEqual([60, 63, 67]);
});
test('learn accumulates released notes, rejects fifth, saves intervals and transposes', () => {
  const states = run(
    'mode 1\nlearn\non 0 0 60 90\ntick\noff 0 0 60 0\non 0 0 67 90\non 0 0 71 90\non 0 0 76 90\ntick\non 0 0 79 90\ntick\nsave\nencode\ntype 10\non 0 0 62 100\ntick',
  );
  expect(states[1]!.capture).toEqual([60, 67, 71, 76]);
  expect(states[2]!.status).toBe(1);
  expect(states[3]!.shape).toEqual([0, 7, 11, 16]);
  expect(notes(states[3]!)).toEqual([62, 69, 73, 78]);
});
test('learn empty/stale save rejected, duplicate pitch free, cancel preserves previous template', () => {
  const states = run(
    'learn\nsave\nsnapshot\non 0 0 60 90\non 0 0 60 90\ntick\nsave 1 1\nsnapshot\nsave\nlearn\non 0 0 65 90\ntick\ncancel\nsnapshot',
  );
  expect(states[0]!.result).toBe(-1);
  expect(states[1]!.capture).toEqual([60]);
  expect(states[2]!.result).toBe(-1);
  expect(states[4]!.shape).toEqual([0]);
});
test('record validates version, length, checksum and interval ordering before mutation', () => {
  for (const corruption of ['corrupt 2 1', 'corrupt 4 8', 'corrupt 6 255', 'corrupt 46 1', '']) {
    const [s] = run(
      `learn\non 0 0 60 90\non 0 0 67 90\ntick\nsave\nencode\n${corruption}\ndecode ${corruption ? 48 : 47}\nsnapshot`,
    ).slice(-1);
    expect(s!.result).toBe(-1);
    expect(s!.shape).toEqual([0, 7]);
  }
  const [s] = run('learn\non 0 0 60 90\ntick\nsave\nencode\ndecode\nsnapshot').slice(-1);
  expect(s!.result).toBe(0);
});
test('out-of-range generated chord is rejected as a whole', () => {
  const [s] = run('mode 1\non 0 0 125 90\ntick');
  expect(s!.voices).toEqual([]);
  expect(s!.status).toBe(2);
});
test('retained expansion is identical on retries; stale ACK cannot retire physical input', () => {
  const [a, b, c, d] = run('mode 1\non 0 0 60 90\nhold\nack 99 1\nhold\nack 1 999\ntick\nsnapshot');
  expect(a!.packet).toHaveLength(32);
  expect(a!.packet[1]).toBe(5 | (3 << 8));
  expect(b!.packet).toEqual(a!.packet);
  expect(c!.packet).toEqual(a!.packet);
  expect(d!.queued).toBe(0);
  expect(d!.voices).toEqual(a!.voices);
});
test('lost ACK deadline resets ownership rather than replaying a fresh chord', () => {
  const [a, b] = run('mode 1\non 0 0 60 90\nhold\nblock 900\nhold');
  expect(a!.packet[1]).toBe(5 | (3 << 8));
  expect(b!.reset).not.toBe(0);
  expect(b!.packet[1]).toBe(2);
});
test('CC123 honors pedal, CC121 releases it and centers bend, CC120 clears latch', () => {
  const states = run(
    'on 0 0 60 90\ntick\ncc 0 0 64 127\ntick\ncc 0 0 123 0\ntick\ncc 0 0 121 0\ntick\nlatch 1\non 0 0 64 90\ntick\ncc 0 0 120 0\ntick',
  );
  expect(notes(states[2]!)).toEqual([60]);
  expect(notes(states[3]!)).toEqual([]);
  expect(notes(states[5]!)).toEqual([]);
});
test('output-token exhaustion requests priority reset without partial send', () => {
  const [s] = run('token 0\non 0 0 60 90\ntick');
  expect(s!.reset).toBe(1);
  expect(s!.packet).toEqual([]);
});

test('all twelve selector mappings and close/open/inversion voicings are bounded', () => {
  const shapes = [
    [0, 4, 7],
    [0, 4, 7, 9],
    [0, 2, 7],
    [0, 3, 7, 9],
    [0, 3, 7],
    [0, 5, 7],
    [0, 4, 7, 14],
    [0, 4, 7, 10],
    [0, 3, 6],
    [0, 3, 7, 10],
    [],
    [0, 4, 7, 11],
  ];
  for (let type = 0; type < 12; type++) {
    const [s] = run(`mode 1\non 0 0 ${24 + type} 127\non 0 0 60 90\ntick`);
    expect(notes(s!)).toEqual(shapes[type]!.map((n) => 60 + n));
  }
  for (const [voicing, expected] of [
    [1, [60, 67, 76]],
    [2, [64, 67, 72]],
    [3, [67, 72, 76]],
  ] as const) {
    const [s] = run(`mode 1\nvoicing ${voicing}\non 0 0 60 90\ntick`);
    expect(notes(s!)).toEqual(expected);
  }
});

test('a live voicing edit emits an output-only packet without consuming queued physical notes', () => {
  const [a, b, c] = run('mode 1\non 0 0 60 90\ntick\nvoicing 1\nhold\non 0 0 62 90\ntick\nsnapshot');
  expect(notes(a!)).toEqual([60, 64, 67]);
  expect(notes(b!)).toEqual([60, 67, 76]);
  expect(c!.queued).toBe(1);
});

test('panic and controller reset fall back across ports without exceeding eight commands', () => {
  for (const cc of [120, 121]) {
    const states = run(
      `mode 1\ntype 11\non 0 0 48 90\ncc 0 0 64 127\ntick\ntick\noff 0 0 48 0\ntick\non 0 1 72 90\ncc 0 1 64 127\ntick\ntick\noff 0 1 72 0\ntick\ncc 0 1 ${cc} 0\ntick`,
    );
    const s = states.at(-1)!;
    expect(s.reset).toBe(0);
    expect(notes(s)).toEqual([48, 52, 55, 59]);
    expect(s.packet[1]! >>> 8).toBeLessThanOrEqual(8);
  }
});

test('capacity exhaustion stays quarantined until explicit keys-up recovery', () => {
  const on = Array.from({ length: 129 }, () => 'on 0 0 60 90').join('\n');
  const states = run(`${on}\ntick\non 0 0 64 100\ntick\nrecover\non 0 0 67 100\ntick`);
  expect(states[0]!.reset).not.toBe(0);
  expect(states[1]!.voices).toEqual([]);
  expect(notes(states[2]!)).toEqual([67]);
});

test('valid checksum cannot conceal malformed template fields', () => {
  for (const change of ['corrupt 4 7', 'corrupt 5 1', 'corrupt 6 7', 'corrupt 7 9']) {
    const s = run(`learn\non 0 0 60 90\non 0 0 67 90\ntick\nsave\nencode\n${change}\nresign\ndecode\nsnapshot`).at(-1)!;
    expect(s.result).toBe(-1);
    expect(s.shape).toEqual([0, 7]);
  }
});

test('LEARN isolates latch and restores it on cancel', () => {
  const states = run(
    'latch 1\nlearn\non 0 0 60 90\ntick\noff 0 0 60 0\ntick\ncancel\non 0 0 64 90\ntick\noff 0 0 64 0\ntick',
  );
  expect(states[1]!.voices).toEqual([]);
  expect(notes(states[3]!)).toEqual([64]);
});

interface SettingsState {
  mode: number;
  dirty: number;
  status: number;
  storage: number;
  pending: number;
  exists: number;
  unsafe: number;
  shape: number[];
}
function settings(commands: string): SettingsState[] {
  return execFileSync(settingsBinary, { input: commands + '\n', encoding: 'utf8', timeout: 2000 })
    .trim()
    .split('\n')
    .map((s) => JSON.parse(s) as SettingsState);
}
test('SAVE is RAM-immediate, card-deferred, restores after restart and never performs I/O under IRQ mask', () => {
  const [ram, durable, restored] = settings(
    'learn\nkey 60\nkey 67\ntick\nsave\nsnapshot\ntick\nsnapshot\nrestart\nsnapshot',
  );
  expect(ram!.shape).toEqual([0, 7]);
  expect(ram!.dirty).toBe(1);
  expect(ram!.exists).toBe(0);
  expect(durable!.dirty).toBe(0);
  expect(durable!.exists).toBe(1);
  expect(restored!.shape).toEqual([0, 7]);
  expect(restored!.unsafe).toBe(0);
});
test.each(['puterror', 'flusherror'])(
  '%s retains RAM shape and dirty status until explicit successful retry',
  (operation) => {
    const [failed, retried] = settings(
      `learn\nkey 60\nkey 76\ntick\nsave\n${operation} -67\ntick\nsnapshot\n${operation} 0\nretry\ntick\nsnapshot`,
    );
    expect(failed!.shape).toEqual([0, 16]);
    expect(failed!.dirty).toBe(1);
    expect(failed!.storage).toBe(-67);
    expect(retried!.dirty).toBe(0);
    expect(retried!.storage).toBe(0);
    expect(retried!.unsafe).toBe(0);
  },
);
test('UI SAVE rejects a changed capture since the displayed revision and NO cancels', () => {
  const [stale, cancelled] = settings('learn\nkey 60\ntick\nkey 67\nsave\nsnapshot\nback\nsnapshot');
  expect(stale!.mode).toBe(2);
  expect(stale!.status).toBe(4);
  expect(stale!.shape).toEqual([]);
  expect(cancelled!.mode).toBe(0);
  expect(cancelled!.dirty).toBe(0);
});
test('failed LEARN menu opening restores PLAY instead of leaving invisible capture active', () => {
  const [s] = settings('openerror 1\nlearn\nsnapshot');
  expect(s!.mode).toBe(0);
  expect(s!.dirty).toBe(0);
});

test('source or fault suspension cancels isolated capture and preserves saved memory', () => {
  const [s] = settings('learn\nkey 60\ntick\nsuspend\ntick\nsnapshot');
  expect(s!.mode).toBe(0);
  expect(s!.shape).toEqual([]);
  expect(s!.dirty).toBe(0);
});

test('direct controller plus unflushed UI diff fails within its output buffer', () => {
  const [s] = run(
    'mode 1\nlearn\non 0 0 60 90\non 0 0 61 90\non 0 0 62 90\non 0 0 63 90\ntick\nsave\ntype 11\non 0 0 60 90\ntick\ntype 10\nvoicing 3\ndirectcc 120\nsnapshot',
  ).slice(-1);
  expect(s!.result).toBe(-1);
});

test('mapped phrases survive retry and same-chord latch replacement commits without note retrigger', () => {
  const [a, release, b, retry] = run(
    'mode 1\nlatch 1\non 0 0 60 90\ntick\noff 0 0 60 0\ntick\non 0 0 60 90\nhold\nhold',
  );
  expect(release!.voices).toEqual(a!.voices);
  expect(b!.voices).toEqual(a!.voices);
  expect(b!.packet[1]).toBe(5 | (1 << 8));
  expect(b!.packet[10]).toBe(5); // metadata-only commit
  expect(b!.packet[4]! >>> 31).toBe(1);
  expect(b!.packet[4]! & 0x7fffffff).toBe((a!.packet[4]! & 0x7fffffff) + 1);
  expect(retry!.packet).toEqual(b!.packet);
});

test('PLAY chord edits retain phrase while fresh attack after silence advances it', () => {
  const [a, b, release, c] = run(
    'on 0 0 60 90\ntick\non 0 0 64 90\ntick\noff 0 0 60 0\noff 0 0 64 0\ntick\non 0 0 67 90\ntick',
  );
  expect(a!.packet[4]! >>> 31).toBe(0);
  expect(b!.packet[4]).toBe(a!.packet[4]);
  expect(release!.voices).toEqual([]);
  expect(c!.packet[4]).toBe(a!.packet[4]! + 1);
});
