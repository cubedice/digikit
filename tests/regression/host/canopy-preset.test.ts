import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

let directory: string, codec: string, settings: string;
beforeAll(() => {
  directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-presets-'));
  const root = path.resolve(import.meta.dirname, '../../..');
  const source = path.join(root, 'modules/core/adapters/instrument-play/src/cf');
  codec = path.join(directory, 'codec');
  settings = path.join(directory, 'settings');
  for (const [driver, output, extra] of [
    ['canopy-preset.c', codec, []],
    ['canopy-settings.c', settings, [path.join(source, 'keyboard.c')]],
  ] as const) {
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
        path.join(import.meta.dirname, driver),
        path.join(source, 'preset.c'),
        ...extra,
        '-o',
        output,
      ],
      { timeout: 20_000 },
    );
  }
});
afterAll(() => {
  if (directory) fs.rmSync(directory, { recursive: true, force: true });
});
function run(binary: string, commands: string): Record<string, unknown>[] {
  return execFileSync(binary, { input: commands + '\n', encoding: 'utf8', timeout: 2000 })
    .trim()
    .split('\n')
    .map((s) => JSON.parse(s) as Record<string, unknown>);
}
test('preset byte record restores stable controls and the embedded shape', () => {
  expect(run(codec, 'decode 64\nsnapshot')[0]).toEqual({
    result: 0,
    controls: [10, 3, 127, 64, 500, 127, 8, 4, 3, 100, 75, 2, 1, 0, 127, 127],
    bpm: 300,
    family: 1,
    mode: 1,
    clock: 2,
    shape: [0, 7, 16, 127],
  });
});
test('invalid lengths and checksums leave the existing sound untouched', () => {
  for (const command of ['decode 0', 'decode 63', 'decode 65', 'byte 13 3\ndecode 64']) {
    const [before, after] = run(codec, `decode 64\nsnapshot\n${command}\nsnapshot`);
    expect(after).toEqual({ ...before, result: -1 });
  }
});
test('valid checksum cannot hide bad versions, padding, shape or parameter ranges', () => {
  // Every field class, including each stable control ID and unavailable SOURCE.
  const edits = [
    [4, 2],
    [5, 15],
    [6, 1],
    [7, 1],
    [40, 2],
    [41, 2],
    [42, 3],
    [43, 1],
    [44, 2],
    [45, 255],
    [46, 5],
    [47, 1],
    [48, 0],
    [49, 7],
    [50, 128],
    [51, 1],
    [59, 1],
  ];
  for (let id = 0; id < 16; id++) edits.push([8 + 2 * id, 255]);
  for (const [offset, value] of edits) {
    const [before, after] = run(codec, `decode 64\nsnapshot\nbyte ${offset} ${value}\nresign\ndecode 64\nsnapshot`);
    expect(after, `byte ${offset}`).toEqual({ ...before, result: -1 });
  }
  const [s] = run(codec, 'byte 42 1\nresign\ndecode 64\nsnapshot');
  expect(s!.result).toBe(-1); // MASTER GRID cannot silently accept swing.
});
test('all four slots save only on the UI tick, load independently, and track edits', () => {
  const commands = Array.from(
    { length: 4 },
    (_, i) => `presetslot ${i + 1}\ncolor ${i * 20}\npresetsave\npresetstate\ntick\npresetstate`,
  ).join('\n');
  const states = run(settings, commands + '\ncolor 100\npresetstate\npresetslot 2\npresetload\npresetstate');
  for (let i = 0; i < 4; i++) {
    expect(states[i * 2]!.pending).toBe(i + 1);
    expect(states[i * 2]!.dirty).toBe(1);
    expect(states[i * 2 + 1]!.dirty).toBe(0);
    expect(states[i * 2 + 1]!.durable).toBe((1 << (i + 1)) - 1);
    expect(states[i * 2 + 1]!.unsafe).toBe(0);
  }
  expect(states[8]!.dirty).toBe(1);
  expect(states[9]!.color).toBe(20);
  expect(states[9]!.dirty).toBe(0);
});
test.each(['puterror', 'flusherror'])('%s preserves sound and the previous confirmed preset', (operation) => {
  const [failed, old, retried] = run(
    settings,
    `color 20\npresetsave\ntick\ncolor 90\n${operation} -67\npresetsave\ntick\npresetstate\n` +
      `presetload\npresetstate\ncolor 90\n${operation} 0\npresetsave\ntick\npresetstate`,
  );
  expect(failed).toMatchObject({ color: 90, dirty: 1, result: -67, unsafe: 0 });
  expect(old!.color).toBe(20);
  expect(retried).toMatchObject({ color: 90, dirty: 0, result: 0, unsafe: 0 });
});
test('reboot and corrupt load preserve validation and embedded shape independence', () => {
  const [restored, isolated, corrupt] = run(
    settings,
    'learn\nkey 60\nkey 76\ntick\nsave\ntick\npresetsave\ntick\nrestart\npresetload\npresetstate\n' +
      'learn\nkey 60\nkey 67\ntick\nsave\ntick\npresetload\npresetstate\n' +
      'corruptpreset 4\nrestart\ncolor 95\npresetload\npresetstate',
  );
  expect(restored!.shape).toEqual([0, 16]);
  expect(isolated!.shape).toEqual([0, 16]);
  expect(isolated!.override).toBe(1);
  expect(corrupt!.color).toBe(95);
  expect(corrupt!.present).toBe(0);
});
test('preset actions cannot interrupt an active LEARN session', () => {
  const [s] = run(settings, 'presetsave\ntick\nlearn\nkey 60\npresetsave\npresetload\nsnapshot');
  expect(s!.mode).toBe(2);
  expect(s!.dirty).toBe(0);
});

test('deferred save retains the sound and destination selected when SAVE was pressed', () => {
  const [edited, saved] = run(
    settings,
    'color 20\npresetsave\ncolor 90\npresetslot 2\ntick\npresetstate\npresetslot 1\npresetload\npresetstate',
  );
  expect(edited).toMatchObject({ color: 90, dirty: 1, present: 1 });
  expect(saved).toMatchObject({ color: 20, dirty: 0 });
});

test('routing restores only after admission and cannot bypass the DSP handshake', () => {
  const [waiting, restored] = run(
    settings,
    'enable 1\nusb 0\nchannel 16\nselectorchannel 5\nselectoroctave -1\nroutesave\ntick\n' +
      'admitted 0\nrestart\nroutestate\nadmitted 1\ntick\nroutestate',
  );
  expect(waiting).toMatchObject({ enabled: 0, channel: 1, restore: 1, calls: 0 });
  expect(restored).toMatchObject({
    enabled: 1,
    usb: 0,
    din: 1,
    channel: 16,
    selector: 5,
    octave: -1,
    restore: 0,
    calls: 0,
    unsafe: 0,
  });
});

test('a user route edit cancels deferred restoration', () => {
  const [s] = run(
    settings,
    'enable 1\nchannel 16\nroutesave\ntick\nadmitted 0\nrestart\nchannel 4\nadmitted 1\ntick\nroutestate',
  );
  expect(s).toMatchObject({ enabled: 0, channel: 4, restore: 0, calls: 0 });
});

test.each(['puterror', 'flusherror'])('routing %s keeps working values and can be retried', (operation) => {
  const [failed, restored, retried] = run(
    settings,
    `channel 4\nroutesave\ntick\nchannel 16\n${operation} -67\nroutesave\ntick\nroutestate\n` +
      `restart\nroutestate\n${operation} 0\nchannel 16\nroutesave\ntick\nrestart\nroutestate`,
  );
  expect(failed).toMatchObject({ channel: 16, result: -67, unsafe: 0 });
  expect(restored).toMatchObject({ channel: 4, result: 0 });
  expect(retried).toMatchObject({ channel: 16, result: 0, unsafe: 0 });
});

test('corrupt routing cannot enable the keyboard or partially load routes', () => {
  for (const offset of [0, 2, 3, 4, 7, 8, 10, 14, 15]) {
    const [s] = run(settings, `enable 1\nchannel 16\nroutesave\ntick\ncorruptroute ${offset}\nrestart\nroutestate`);
    expect(s).toMatchObject({ enabled: 0, usb: 1, din: 1, channel: 1, restore: 0, calls: 0 });
    expect(s!.result).toBe(-1);
  }
});

test('sound preset loading and saving do not replace device routing', () => {
  const [s] = run(settings, 'channel 4\nroutesave\ntick\npresetsave\ntick\nchannel 16\npresetload\nroutestate');
  expect(s).toMatchObject({ channel: 16, unsafe: 0 });
});

test('a valid routing checksum cannot conceal unsupported schema or channel ranges', () => {
  for (const offset of [2, 3, 7, 10]) {
    const [s] = run(
      settings,
      `enable 1\nchannel 16\nroutesave\ntick\ncorruptroute ${offset}\nresignroute\nrestart\nroutestate`,
    );
    expect(s).toMatchObject({ enabled: 0, channel: 1, restore: 0, calls: 0, result: -1 });
  }
});
