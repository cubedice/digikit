import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-live-transaction-'));
const executable = path.join(directory, 'live-transaction');

beforeAll(() => {
  const root = path.resolve(import.meta.dirname, '../../..');
  const source = path.join(root, 'modules/core/adapters/instrument-play/src/cf');
  execFileSync(
    'cc',
    [
      '-std=c11',
      '-DDK_CANOPY_SOUND_PROOF=1',
      '-Wall',
      '-Wextra',
      '-Werror',
      '-fsanitize=undefined',
      '-I',
      path.join(root, 'sdk/include'),
      path.join(import.meta.dirname, 'canopy-live-transaction.c'),
      path.join(root, 'tests/regression/fixtures/canopy_keyboard_proof.c'),
      ...[
        'preset.c',
        'src-state.c',
        'src-lock-packed.c',
        'status.c',
        'src-display.c',
        'sound-codec.c',
        'sound-pool.c',
      ].map((file) => path.join(source, file)),
      '-o',
      executable,
    ],
    { timeout: 20_000 },
  );
});

afterAll(() => fs.rmSync(directory, { recursive: true, force: true }));

test.each([
  ['encoder', 'publishes immutable snapshots through the public encoder or refuses without mutation'],
  ['native', 'passes native controls and list remainders without consuming reserved snapshots'],
  ['midi', 'keeps musical notes and releases working when selectors are refused, then retries'],
  ['track', 'attaches to the mapped keyboard track while another instance owns the UI'],
  ['identity', 'refuses stale owner identity and failed preparation without publishing or consuming reservations'],
  ['admission', 'filters selectors with unavailable admission or binding while preserving notes and releases'],
  ['learn', 'passes unmapped input and captures LEARN notes without invoking persistence callbacks'],
  ['disabled', 'rejects partial registration and restores ordinary APIs when callbacks are disabled'],
  ['reentry', 'refuses nested encoder, MIDI, and registration calls without corrupting the outer edit'],
])('%s: %s', (scenario) => {
  expect(() => execFileSync(executable, [scenario], { timeout: 10_000 })).not.toThrow();
});
