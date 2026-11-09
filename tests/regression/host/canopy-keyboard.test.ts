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

test('PANEL keys retain independent ownership beside identical USB and DIN pitches', () => {
  const [held, panelUp, usbUp, released] = run(
    'panelon 0 60 90\non 0 0 60 80\non 0 1 60 70\ntick\npaneloff 0\ntick\noff 0 0 60 0\ntick\noff 0 1 60 0\ntick',
  );
  expect(held!.voices.map((v) => v[3]).sort()).toEqual([0, 1, 2]);
  expect(panelUp!.voices.map((v) => v[3]).sort()).toEqual([0, 1]);
  expect(usbUp!.voices.map((v) => v[3])).toEqual([1]);
  expect(released!.voices).toEqual([]);
});

test('PANEL release uses physical key identity through octave and scale changes', () => {
  const [held, remaining, released] = run(
    'panelon 0 60 90\npanelon 1 64 90\npanelon 2 67 90\npanelon 0 72 90\ntick\npaneloff 1\ntick\npaneloff 0\npaneloff 2\ntick',
  );
  expect(notes(held!)).toEqual([60, 64, 67]);
  expect(notes(remaining!)).toEqual([60, 67]);
  expect(released!.voices).toEqual([]);
});

test('PANEL cancel clears latched local voices without releasing MIDI or sharing its pedal', () => {
  const [held, cancelled, afterOldUp] = run(
    'cc 0 0 64 127\ntick\npanelon 0 60 90\non 0 0 64 90\ntick\npanelcancel\ntick\npaneloff 0\ntick',
  ).slice(1);
  expect(notes(held!)).toEqual([60, 64]);
  expect(notes(cancelled!)).toEqual([64]);
  expect(afterOldUp!.voices).toEqual(cancelled!.voices);
  const [latched, clear] = run('latch 1\npanelon 0 67 90\npaneloff 0\ntick\npanelcancel\ntick');
  expect(notes(latched!)).toEqual([67]);
  expect(clear!.voices).toEqual([]);
});

test('PANEL generation tombstones cannot release a fresh same-pitch key', () => {
  const [old, fresh, afterOldUp, clear] = run(
    'panelon 0 60 90\ntick\nreset\npanelon 1 60 90\ntick\npaneloff 0\ntick\npaneloff 1\ntick',
  );
  expect(notes(old!)).toEqual([60]);
  expect(notes(fresh!)).toEqual([60]);
  expect(afterOldUp!.voices).toEqual(fresh!.voices);
  expect(clear!.voices).toEqual([]);
});

test('PANEL runs ROOT and LEARN without a MIDI selector channel', () => {
  const [root, up] = run('mode 1\nselector 0\npanelon 0 60 100\ntick\npaneloff 0\ntick');
  expect(notes(root!)).toEqual([60, 64, 67]);
  expect(up!.voices).toEqual([]);
  const [learned] = run('learn\npanelon 0 60 90\npanelon 1 64 90\npanelon 2 67 90\ntick');
  expect(learned!.capture).toEqual([60, 64, 67]);
});
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

test('mod wheel retains its external route through PLAY and ROOT without changing voices or phrase', () => {
  for (const mode of [0, 1])
    for (const port of [0, 1])
      for (const channel of [0, 15]) {
        const [held, wheel, retry, acked, unchanged] = run(
          `mode ${mode}\non ${channel} ${port} 60 100\ntick\ncc ${channel} ${port} 1 93\nhold\nhold\ntick\nsnapshot`,
        );
        expect(wheel!.voices).toEqual(held!.voices);
        expect(wheel!.packet[4]).toBe(held!.packet[4]);
        expect(wheel!.packet[1]).toBe(5 | (1 << 8));
        expect(wheel!.packet[8]).toBe(0);
        expect(wheel!.packet[10]).toBe(3 | (port << 4) | (channel << 6) | (1 << 14) | (93 << 21));
        expect(retry!.packet).toEqual(wheel!.packet);
        expect(acked!.packet).toEqual(wheel!.packet);
        expect(unchanged!.queued).toBe(0);
        expect(unchanged!.voices).toEqual(held!.voices);
      }
});

test('controller reset clears only the route wheel while preserving mapped latch; sound off retains wheel', () => {
  const [held, reset, panic] = run('latch 1\non 15 1 60 90\ntick\ncc 15 1 121 0\ntick\ncc 15 1 120 0\ntick');
  expect(reset!.voices).toEqual(held!.voices);
  expect(reset!.packet[1]).toBe(5 | (2 << 8));
  expect(reset!.packet[8]).toBe(0);
  expect(reset!.packet[11]).toBe(0);
  expect(reset!.packet[10]).toBe(4 | (1 << 4) | (15 << 6) | (64 << 21));
  expect(reset!.packet[13]).toBe(3 | (1 << 4) | (15 << 6) | (1 << 14));
  expect(panic!.voices).toEqual([]);
  expect(panic!.packet[1]).toBe(5 | (1 << 8));
  expect(panic!.packet[10]).toBe(3 | (1 << 4) | (15 << 6) | (120 << 14));
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

test.each([1, 2, 3])('USER keeps learned intervals while retaining generated voicing %i', (voicing) => {
  const states = run(
    `mode 1\nvoicing ${voicing}\nlearn\non 0 0 60 90\non 0 0 67 90\non 0 0 76 90\ntick\n` +
      'save\ntype 10\non 0 0 62 90\ntick\ntype 0\ntick',
  );
  expect(notes(states.at(-2)!)).toEqual([62, 69, 78]);
  const generated = voicing === 1 ? [62, 69, 78] : voicing === 2 ? [66, 69, 74] : [69, 74, 78];
  expect(notes(states.at(-1)!)).toEqual(generated);
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
test('Status wraps a long mode/chord pair without clipping either part', () => {
  const rows = JSON.parse(
    execFileSync(settingsBinary, {
      input: 'statuswrap\n',
      encoding: 'utf8',
      timeout: 2000,
    }),
  );
  expect(rows).toEqual(['ROOT', 'C#maj7/G# +123']);
});
test('CANOPY Settings exposes bounded native Status rows and clears stale performance state', () => {
  const [fresh, stale] = execFileSync(settingsBinary, {
    input: 'statusview 1\nstatusview 0\n',
    encoding: 'utf8',
    timeout: 2000,
  })
    .trim()
    .split('\n')
    .map((s) => JSON.parse(s) as { owns: number; foreign: number; entry: string; visible: number; rows: string[] });
  expect(fresh!.owns).toBe(1);
  expect(fresh!.foreign).toBe(0);
  expect(fresh!.entry).toBe('STATUS');
  expect(fresh!.visible).toBe(0);
  expect(fresh!.rows.slice(1, 5)).toEqual(['C#-1 D#-1', 'G#-1 A#-1', 'LATCH HOLDING', 'PEDAL DOWN']);
  expect(fresh!.rows.every((row) => row.length < 24)).toBe(true);
  expect(stale!.rows[0]).toContain('LINK LOST');
  expect(stale!.rows.slice(1, 5)).toEqual(['-- --', '-- --', 'LATCH --', 'PEDAL --']);
});
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
test.each([0, 1, 2, 3, 4])('LEARN YES saves from informational row %s', (row) => {
  const [saved] = settings(`learn\nkey 0\nkey 13\ntick\nyes ${row}\nsnapshot`);
  expect(saved!.shape).toEqual([0, 13]);
  expect(saved!.mode).toBe(0);
  expect(saved!.dirty).toBe(1);
  expect(saved!.exists).toBe(0);
});

test('LEARN YES refreshes a changed capture before allowing a second press to save', () => {
  const [stale, saved] = settings('learn\nkey 60\ntick\nkey 67\nyes 0\nsnapshot\nyes 0\nsnapshot');
  expect(stale!.mode).toBe(2);
  expect(stale!.shape).toEqual([]);
  expect(saved!.shape).toEqual([0, 7]);
  expect(saved!.mode).toBe(0);
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
  const states = run(
    'mode 1\nlearn\non 0 0 60 90\non 0 0 61 90\non 0 0 62 90\non 0 0 63 90\ntick\nsave\n' +
      'type 11\nvoicing 3\non 0 0 60 90\ntick\ntype 10\ndirectcc 120\nsnapshot',
  );
  const before = states.at(-2)!;
  const s = states.at(-1)!;
  // The generated inversion shares no pitch with learned [60,61,62,63].
  // Four offs, four ons and the other route's controller require nine outputs.
  expect(notes(before)).toEqual([67, 71, 72, 76]);
  expect(s!.result).toBe(-1);
  expect(s.voices).toEqual(before.voices);
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
