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
    [
      'canopy-settings.c',
      settings,
      ['keyboard.c', 'src-state.c', 'src-lock-packed.c', 'status.c', 'src-display.c'].map((f) => path.join(source, f)),
    ],
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
  expect(run(codec, 'decode 144\nsnapshot')[0]).toEqual({
    result: 0,
    controls: [
      10, 3, 127, 64, 500, 127, 8, 4, 3, 100, 75, 2, 0, 0, 127, 127, 0, 0, 0, 10, 0, 0, 0, 100, 5, 15, 100, 100, 0, 3,
      100, 100,
    ],
    bpm: 300,
    family: 1,
    mode: 1,
    clock: 2,
    shape: [0, 7, 16, 127],
    fm: [32, 0, 50, 3, 8, 0, 5, 40, 50, 15, 100, 100, 100],
  });
});

test.each([1, 2, 3, 4, 5])('preset v%i discards old latch-on and saves a canonical reserved zero', (version) => {
  const length = version <= 2 ? 64 : version === 3 ? 96 : version === 4 ? 128 : 144;
  const migration = version < 5 ? `legacy ${version}\n` : '';
  const [off, on, record, restored] = run(
    codec,
    `${migration}resign\ndecode ${length}\nsnapshot\nbyte 33 1\nresign\ndecode ${length}\nsnapshot\nencode\nrecord\ndecode 144\nsnapshot`,
  );
  expect(off!.result).toBe(0);
  expect(on).toEqual(off);
  expect((on!.controls as number[])[12]).toBe(0);
  expect(record).toEqual({ version: 5, reserved: 0 });
  expect(restored).toEqual(on);
});

test.each([2, 255])('old latch value %i outside the original range is rejected without changing the sound', (value) => {
  const [before, after] = run(codec, `decode 144\nsnapshot\nbyte 33 ${value}\nresign\ndecode 144\nsnapshot`);
  expect(after).toEqual({ ...before, result: -1 });
});

test('active preset validation rejects attempts to save a nonzero reserved latch field', () => {
  const [invalid, record] = run(codec, 'decode 144\nreserved 1\nencode\nsnapshot\nrecord');
  expect(invalid!.result).toBe(-1);
  expect(record).toEqual({ version: 5, reserved: 0 });
});
test('SRC encoder edits use the real preset/configuration bank without enabling arp', () => {
  const [edited, restored, stale, movement] = run(
    settings,
    'srcpage 2\nsrcturn 0 12\nsrcstate\npresetsave\ntick\ncolor 10\npresetload\nsrcstate\n' +
      'srcturn 0 1\nsrcstate\nsrcpage 1\nsrcturn 0 4\nsrcstate',
  );
  expect(edited).toMatchObject({ result: 1, color: 76, value: '76', dirty: 1, heading: 'LINK LOST' });
  expect(restored).toMatchObject({ color: 76, value: '76' });
  expect(stale).toMatchObject({ result: -1, color: 76 });
  expect(movement).toMatchObject({ result: 1, rate: 5, mix: 0 });
});

test('SRC waits for a full list step and cannot edit sound values during LEARN', () => {
  const [play, modal, closed] = run(
    settings,
    'srcpage 0\nsrcturn 1 1\nsrcstate\nlearn\nsrcpage 2\nsrcturn 0 10\nsrcstate\n' + 'back\nsrcturn 0 10\nsrcstate',
  );
  expect(play).toMatchObject({ result: 0, type: 0 });
  expect(modal).toMatchObject({ result: 0, color: 64 });
  expect(closed).toMatchObject({ result: -1, color: 64 });
});

test.each(['', 'learn\nkey 60\nkey 67\nkey 76\ntick\nsave\ntick\n'])(
  'SRC can leave USER in either direction without changing learned memory (%s)',
  (learn) => {
    const [user, lower, userAgain, upper, memory] = run(
      settings,
      learn +
        'mode 1\nsrcpage 0\nsrcturn 1 40\nsrcstate\n' +
        'srcpage 0\nsrcturn 1 -4\nsrcstate\nsrcpage 0\nsrcturn 1 4\nsrcstate\n' +
        'srcpage 0\nsrcturn 1 4\nsrcstate\nsnapshot',
    );
    expect(user).toMatchObject({ result: 1, type: 10 });
    expect(lower).toMatchObject({ result: 1, type: 9 });
    expect(userAgain).toMatchObject({ result: 1, type: 10 });
    expect(upper).toMatchObject({ result: 1, type: 11 });
    expect(memory).toMatchObject({ mode: 1, shape: learn ? [0, 7, 16] : [], unsafe: 0 });
  },
);

test('a preset loaded with USER can select a standard chord and return without losing its embedded shape', () => {
  const [loaded, standard, back, preserved] = run(
    settings,
    'learn\nkey 60\nkey 76\ntick\nsave\ntick\nmode 1\ntype 10\npresetsave\ntick\n' +
      'type 0\npresetload\npresetstate\nsrcpage 0\nsrcturn 1 -40\nsrcstate\n' +
      'srcpage 0\nsrcturn 1 40\nsrcstate\npresetstate',
  );
  expect(loaded).toMatchObject({ override: 1, shape: [0, 16] });
  expect(standard).toMatchObject({ result: 1, type: 0 });
  expect(back).toMatchObject({ result: 1, type: 10 });
  expect(preserved).toMatchObject({ override: 1, shape: [0, 16], unsafe: 0 });
});

test('CHRD changes performance mode with one reset and changes root types live', () => {
  const [play, rootBoundary, root, changed, playBoundary, direct] = run(
    settings,
    'key 60\nvoices\nsrcpage 0\nsrcturn 1 4\nvoices\nkey 60\nvoices\n' +
      'srcpage 0\nsrcturn 1 4\nvoices\nsrcpage 0\nsrcturn 1 -8\nvoices\nkey 62\nvoices',
  );
  const notes = (row: Record<string, unknown> | undefined) => (row!.voices as number[][]).map((v) => v[0]);
  expect(notes(play)).toEqual([60]);
  expect(rootBoundary).toMatchObject({ mode: 1, type: 0, voices: [], generation: (play!.generation as number) + 1 });
  expect(notes(root)).toEqual([60, 64, 67]);
  expect(changed).toMatchObject({ mode: 1, type: 1, generation: root!.generation });
  expect(notes(changed)).toEqual([60, 64, 67, 69]);
  expect((changed!.voices as number[][]).slice(0, 3)).toEqual(root!.voices);
  expect(playBoundary).toMatchObject({ mode: 0, voices: [], generation: (root!.generation as number) + 1 });
  expect(notes(direct)).toEqual([62]);
});

test('CHRD modes and WAVE families survive preset save without changing routing', () => {
  const [reed, wave, legacy, restored, routes, direct] = run(
    settings,
    'channel 4\nsrcpage 0\nsrcturn 1 4\nsrcpage 0\nsrcturn 3 4\nconfig\n' +
      'srcpage 0\nsrcturn 3 16\nconfig\npresetsave\ntick\n' +
      'srcpage 0\nsrcturn 3 -20\nconfig\npresetload\nconfig\nroutestate\n' +
      'srcpage 0\nsrcturn 1 -4\npresetsave\ntick\nmode 1\npresetload\nvoices',
  );
  expect((reed!.values as number[])[1]).toBe(1);
  expect((wave!.values as number[])[1]).toBe(2);
  expect((wave!.values as number[])[16]! >>> 16).toBe(3);
  expect((legacy!.values as number[])[1]).toBe(0);
  expect((legacy!.values as number[])[16]! >>> 16).toBe(3);
  expect(restored!.values).toEqual(wave!.values);
  expect(routes).toMatchObject({ channel: 4, unsafe: 0 });
  expect(direct).toMatchObject({ mode: 0, family: 2, table: 3 });
});

test('compact menus keep non-SRC settings accessible and omit duplicate performance controls', () => {
  const [layout, route, config] = run(
    settings,
    'menus\nmenuset 0 0 1\nmenuset 0 3 9\nmenuset 0 5 2\nmenuset 0 6 6\n' +
      'srcpage 1\nsrcturn 7 21\nfamily 2\nsrcpage 2\nsrcturn 4 -40\nsrcturn 5 40\nsrcturn 6 45\nsrcturn 7 20\n' +
      'menuset 2 2 -30\nmenuset 2 3 60\nmenuset 2 4 -20\n' +
      'routestate\nconfig',
  );
  expect(layout!.entry).toBe('CANOPY');
  expect(layout!.home).toEqual(['INPUT', 'CHORD LEARN', 'MODULATION', 'PRESETS']);
  const children = layout!.children as string[][];
  expect(children[0]!.slice(0, 9)).toEqual([
    'EXTERNAL MIDI',
    'USB',
    'DIN',
    'CHANNEL',
    'SAVE ROUTING',
    'SELECTOR OCTAVE',
    'SELECT CH (0=SAME)',
    'PANIC',
    'RECOVER KEYBOARD',
  ]);
  expect(children[1]!.slice(0, 4)).toEqual(['SHAPE SLOT', 'LEARN TO ROOT', 'SHAPES SAVED', 'RETRY SHAPE SAVE']);
  expect(children[2]).toEqual(['FREE HZ', 'FREE', 'WHEEL TO POS', 'WHEEL TO FM', 'WHEEL TO WT FM']);
  const labels = children.flat();
  for (const duplicate of [
    'PLAY',
    'ROOT + TYPE',
    'LATCH',
    'MAJOR',
    'CLOSE',
    'VELVET',
    'COLOR',
    'DRIFT',
    'ARP MIX',
    'ARP CLOCK',
    'LOCAL BPM',
    'NOTES / Q',
    'ARP GATE',
    'UP',
    'OCTAVE SPREAD',
    'SWING',
    'TRIG',
    'STRUM MS',
    'ANALOG',
    'AMP ATTACK',
    'AMP RELEASE',
  ]) {
    expect(labels).not.toContain(duplicate);
  }
  expect(route).toMatchObject({ enabled: 1, channel: 9, octave: 2, selector: 6, unsafe: 0 });
  const values = config!.values as number[];
  expect(values[4]).toBe(5); // Legacy preset fields remain readable, not menu controls.
  expect(values[5]).toBe(15);
  expect(values[7]).toBe(120); // Blank ARP H cannot change the compatibility field.
  expect(values.slice(23, 28).map((v) => v >>> 16)).toEqual([60, 45, 60, 120, 70]);
  expect(values[31]! >>> 16).toBe(80);
});

test('invalid lengths and checksums leave the existing sound untouched', () => {
  for (const command of ['decode 0', 'decode 63', 'decode 65', 'byte 13 3\ndecode 144']) {
    const [before, after] = run(codec, `decode 144\nsnapshot\n${command}\nsnapshot`);
    expect(after).toEqual({ ...before, result: -1 });
  }
});

test.each([1, 2, 3])('old preset with %i total octaves preserves its upward spread', (old) => {
  const [decoded] = run(codec, `legacy 1\nbyte 25 ${old}\nresign\ndecode 64\nsnapshot`);
  expect(decoded!.result).toBe(0);
  expect((decoded!.controls as number[])[8]).toBe(old + 2);
});

test.each([0, 3, 6])('new preset preserves encoded signed spread %i', (spread) => {
  const [decoded] = run(codec, `byte 25 ${spread}\nresign\ndecode 144\nsnapshot`);
  expect(decoded!.result).toBe(0);
  expect((decoded!.controls as number[])[8]).toBe(spread);
});

test.each([
  [1, 0],
  [1, 4],
  [2, 7],
])('preset version %i rejects invalid spread %i atomically', (version, spread) => {
  const [before, after] = run(
    codec,
    `decode 144\nsnapshot\nlegacy ${version}\nbyte 25 ${spread}\nresign\ndecode 64\nsnapshot`,
  );
  expect(after).toEqual({ ...before, result: -1 });
});

test('SRC signed spread is saved and restored through the real control bank', () => {
  const [negative, positive, restored] = run(
    settings,
    'srcpage 1\nsrcturn 2 -12\nconfig\npresetsave\ntick\nsrcpage 1\nsrcturn 2 24\nconfig\npresetload\nconfig',
  );
  for (const [row, encoded] of [
    [negative, 0],
    [positive, 6],
    [restored, 0],
  ] as const) {
    expect(row!.schema).toBe(0x43505342);
    expect((row!.values as number[])[11]).toBe(encoded);
    expect((row!.values as number[])[24]! & 0xffff).toBe(encoded);
  }
});
test('valid checksum cannot hide bad versions, padding, shape or parameter ranges', () => {
  // Every field class, including each stable control ID and unavailable SOURCE.
  const edits = [
    [4, 6],
    [5, 15],
    [6, 1],
    [7, 1],
    [40, 4],
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
    const [before, after] = run(codec, `decode 144\nsnapshot\nbyte ${offset} ${value}\nresign\ndecode 144\nsnapshot`);
    expect(after, `byte ${offset}`).toEqual({ ...before, result: -1 });
  }
  const [s] = run(codec, 'byte 42 1\nresign\ndecode 144\nsnapshot');
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

test('audio submission and SRC pending markers share the full configuration revision', () => {
  const [first, unchanged, mode, tempo] = run(
    settings,
    'color 100\nconfig\ntick\nconfig\nmode 1\nconfig\nbpm 140\nconfig',
  );
  expect(first).toMatchObject({ schema: 0x43505342, count: 32, pending: 1 << 2 }); // Only COLOR changed from the initialized sound.
  expect(first!.revision).toBe(first!.srcRevision);
  expect((first!.values as number[]).slice(16).map((v) => v & 0xffff)).toEqual([
    0, 0, 100, 0, 0, 0, 4, 0, 3, 50, 0, 0, 0, 0, 5, 15,
  ]);
  expect((first!.values as number[])[2]).toBe(100);
  expect(unchanged!.revision).toBe(first!.revision);
  expect(mode!.revision as number).toBeGreaterThan(first!.revision as number);
  // Bit 15 flags the DSP status focus (the selected instance); the adapter strips it.
  expect((mode!.values as number[])[0]).toBe(0x4000810b);
  // An old in-memory local BPM cannot alter the fixed pattern clock or dirty SRC.
  expect(tempo!.revision).toBe(mode!.revision);
  expect((tempo!.values as number[])[7]).toBe(120);
  expect((tempo!.values as number[])[13]).toBe(2);
});

test.each([0, 1, 2])('loading former arp clock %i uses pattern tempo and leaves the retired knobs blank', (clock) => {
  const [loaded, state, afterTurn, saved, restored] = run(
    settings,
    `legacyarp ${clock}\npresetload\nconfig\npresetstate\nsrcpage 1\nsrcturn 6 -1000\nsrcturn 7 1000\nconfig\n` +
      'presetsave\ntick\nconfig\npresetload\nconfig',
  );
  const values = loaded!.values as number[];
  expect(values[7]).toBe(120);
  expect(values[8]).toBe(6);
  expect(values[13]).toBe(2);
  expect(state).toMatchObject({ dirty: 0, unsafe: 0 });
  expect(afterTurn!.revision).toBe(loaded!.revision);
  expect(afterTurn!.values).toEqual(values);
  expect(saved!.values).toEqual(values);
  expect(restored!.values).toEqual(values);
});

test.each([1, 2])('legacy v%i initializes wavetable defaults without selecting it', (version) => {
  const [s] = run(codec, `legacy ${version}\nresign\ndecode 64\nsnapshot`);
  expect(s).toMatchObject({ result: 0, family: 1 });
  expect((s!.controls as number[]).slice(16)).toEqual([0, 0, 0, 10, 0, 0, 0, 100, 5, 15, 100, 100, 0, 3, 100, 100]);
});

test('wavetable presets retain position, modulation and legacy color independently', () => {
  const [saved, restored, legacy] = run(
    settings,
    'family 2\nsrcpage 2\nsrcturn 0 350\nsrcturn 1 55\nconfig\npresetsave\ntick\n' +
      'srcpage 2\nsrcturn 0 200\ncolor 20\npresetload\nconfig\nfamily 0\nsrcpage 2\nsrcstate',
  );
  expect((saved!.values as number[])[1]).toBe(2);
  expect((saved!.values as number[])[17]! >>> 16).toBe(350);
  expect((saved!.values as number[])[18]! >>> 16).toBe(55);
  expect(restored!.values).toEqual(saved!.values);
  expect(legacy).toMatchObject({ color: 64, value: '64' });
});

test('sync scan rate uses its own clock domain and clamps to musical divisions', () => {
  const [sync, free] = run(
    settings,
    'family 2\nscanclock 1\nsrcpage 2\nsrcturn 2 1000\nconfig\nscanclock 0\nsrcpage 2\nsrcturn 2 1000\nconfig',
  );
  expect((sync!.values as number[])[19]! >>> 16).toBe(7);
  expect((sync!.values as number[])[22]! >>> 16).toBe(1);
  expect((free!.values as number[])[19]! >>> 16).toBe(1000);
  expect((free!.values as number[])[22]! >>> 16).toBe(0);
});

test.each([
  [88, 2],
  [89, 1],
  [91, 1],
  [64, 1],
  [66, 3],
  [68, 1],
  [70, 4],
  [72, 1],
  [74, 1],
  [76, 1],
  [78, 1],
  [80, 1],
  [82, 1],
  [84, 1],
  [86, 1],
])('new preset rejects unsupported table version/padding/control (%i,%i)', (offset, value) => {
  const [before, after] = run(codec, `decode 144\nsnapshot\nbyte ${offset} ${value}\nresign\ndecode 144\nsnapshot`);
  expect(after).toEqual({ ...before, result: -1 });
});

test.each([1, 2, 3, 4])('legacy on-card v%i is canonicalized before load and dirty comparison', (version) => {
  const [restored, canonical] = run(
    settings,
    `color 76\npresetsave\ntick\nlegacypreset ${version}\nrestart\ncolor 2\npresetload\npresetstate\npresetsave\ntick\nrestart\npresetload\npresetstate`,
  );
  expect(restored).toMatchObject({ color: 76, dirty: 0, present: 1, unsafe: 0 });
  expect(canonical).toMatchObject({ color: 76, dirty: 0, present: 1, unsafe: 0 });
});

test.each([1, 2, 3])('preset v%i migrates FM defaults without changing the remembered legacy sound', (version) => {
  const [decoded] = run(codec, `legacy ${version}\nresign\ndecode ${version <= 2 ? 64 : 96}\nsnapshot`);
  expect(decoded).toMatchObject({ result: 0, family: 1, fm: [32, 0, 50, 3, 8, 0, 5, 40, 50, 15, 100, 100, 100] });
  expect((decoded!.controls as number[])[2]).toBe(127);
});

test.each([
  [93, 128],
  [95, 4],
  [97, 101],
  [99, 16],
  [101, 16],
  [103, 101],
  [105, 128],
  [107, 128],
  [109, 101],
  [111, 128],
  [113, 201],
  [115, 201],
  [117, 201],
  [118, 2],
  [119, 1],
  [129, 1],
])('inactive remembered FM bank rejects invalid field %i=%i', (offset, value) => {
  const [before, after] = run(codec, `decode 144\nsnapshot\nbyte ${offset} ${value}\nresign\ndecode 144\nsnapshot`);
  expect(after).toEqual({ ...before, result: -1 });
});

test('FM and wavetable sound memories survive source changes and v5 preset save/load', () => {
  const [wavetable, fm, recalled, legacy, wtAgain] = run(
    settings,
    'color 77\nfamily 2\nsrcpage 2\nsrcturn 0 650\nsrcturn 2 989\nconfig\n' +
      'srcpage 0\nsrcturn 3 20\nsrcpage 2\nsrcturn 1 12\nsrcturn 4 48\nsrcturn 5 28\n' +
      'srcpage 2\nsrcturn 0 67\nsrcpage 3\nsrcturn 0 13\nsrcturn 2 25\nconfig\npresetsave\ntick\n' +
      'family 0\ncolor 10\npresetload\nconfig\nfamily 0\nconfig\nfamily 2\nconfig',
  );
  const words = fm!.values as number[];
  expect(words[0]! & 255).toBe(11);
  expect(words[1]).toBe(3);
  expect(words[2]).toBe(99);
  expect(words.slice(16, 28).map((v) => v >>> 16)).toEqual([3, 50, 15, 15, 0, 18, 40, 75, 15, 100, 100, 100]);
  expect(recalled!.values).toEqual(fm!.values);
  expect((legacy!.values as number[])[2]).toBe(77);
  expect(wtAgain!.values).toEqual(wavetable!.values);
});

test('legacy telemetry cannot acknowledge an active FM bank', () => {
  const [legacy, current] = run(settings, 'family 3\nsrcpage 2\nackdetail 3\nackdetail 5');
  expect(legacy!.pending).toBe(0xffffffff);
  expect(current!.pending).toBe(0);
});

test.each([1, 2, 3, 4])('preset v%i adds neutral wavetable FM without changing its original controls', (version) => {
  const length = version <= 2 ? 64 : version === 3 ? 96 : 128;
  const [decoded] = run(codec, `legacy ${version}\nresign\ndecode ${length}\nsnapshot`);
  expect(decoded).toMatchObject({ result: 0, family: 1 });
  expect((decoded!.controls as number[]).slice(28)).toEqual([0, 3, 100, 100]);
  expect((decoded!.controls as number[])[2]).toBe(127);
  expect(decoded!.fm).toEqual([32, 0, 50, 3, 8, 0, 5, 40, 50, 15, 100, 100, 100]);
});

test.each([
  [121, 101],
  [123, 16],
  [125, 201],
  [127, 201],
  [128, 2],
  [129, 1],
  [139, 1],
])('wavetable FM preset rejects invalid field %i=%i atomically', (offset, value) => {
  const [before, after] = run(codec, `decode 144\nsnapshot\nbyte ${offset} ${value}\nresign\ndecode 144\nsnapshot`);
  expect(after).toEqual({ ...before, result: -1 });
});

test('wavetable FM shares timbre timing, preserves scanning and recalls its controls across FM and legacy sounds', () => {
  const [wave, changed, recalled, legacy, standalone, returned] = run(
    settings,
    'family 2\nsrcpage 2\nsrcturn 0 350\nsrcturn 1 45\nsrcturn 2 30\nsrcturn 5 40\nsrcturn 6 30\n' +
      'srcpage 3\nsrcturn 0 65\nsrcturn 1 20\nsrcturn 2 -60\nmenus\nmenuset 2 4 75\nconfig\npresetsave\ntick\n' +
      'srcpage 3\nsrcturn 0 -20\nconfig\npresetload\nconfig\nfamily 0\nconfig\nfamily 3\nconfig\nfamily 2\nconfig',
  ).slice(1);
  const words = wave!.values as number[];
  expect(words[0]! & 255).toBe(11);
  expect(words.slice(28).map((word) => word >>> 16)).toEqual([65, 8, 40, 175]);
  expect(words.slice(17, 20).map((word) => word >>> 16)).toEqual([350, 45, 40]);
  expect(words.slice(24, 26).map((word) => word >>> 16)).toEqual([45, 45]);
  expect((changed!.values as number[])[28]! >>> 16).toBe(45);
  expect(recalled!.values).toEqual(wave!.values);
  expect((legacy!.values as number[]).slice(28).map((word) => word >>> 16)).toEqual([65, 8, 40, 175]);
  expect((standalone!.values as number[])[2]).toBe(32);
  expect((standalone!.values as number[]).slice(28).map((word) => word >>> 16)).toEqual([65, 8, 40, 175]);
  expect(returned!.values).toEqual(wave!.values);
});

test('old detail schemas acknowledge only old controls while schema5 confirms wavetable FM', () => {
  const [old, current] = run(settings, 'family 2\nsrcpage 3\nsrcturn 0 70\nackdetail 4\nackdetail 5');
  expect(old!.pending).toBe(0xf0000000);
  expect(current!.pending).toBe(0);
});
