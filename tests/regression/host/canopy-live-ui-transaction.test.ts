import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-live-ui-transaction-'));
const executable = path.join(directory, 'live-ui-transaction');

beforeAll(() => {
  const root = path.resolve(import.meta.dirname, '../../..');
  const source = path.join(root, 'modules/core/adapters/instrument-play/src/cf');
  execFileSync('cc', [
    '-std=c11', '-DDK_CANOPY_SOUND_PROOF=1', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
    '-I', path.join(root, 'sdk/include'),
    path.join(import.meta.dirname, 'canopy-live-ui-transaction.c'),
    ...[
      'keyboard.c', 'preset.c', 'src-state.c', 'src-lock-packed.c', 'status.c',
      'src-display.c', 'sound-codec.c', 'sound-pool.c',
    ].map((file) => path.join(source, file)),
    '-o', executable,
  ], { timeout: 20_000 });
});

afterAll(() => fs.rmSync(directory, { recursive: true, force: true }));

test.each([
  ['preset', 'loads through the real preset row and retains pending saves on refusal'],
  ['modulation', 'commits scan clock/rate pairs and restores signed menu amounts on reservation refusal'],
  ['inactive', 'addresses inactive wheel banks and retains reservations after failed preparation'],
  ['shape', 'restores the staged slot and preset override on refusal before retrying the real row'],
  ['shape-same', 'preserves the preset override when the accepted shape slot does not change'],
  ['learn-two', 'prepares two owners before attaching the real learned shape, including a PLAY peer'],
  ['learn-three', 'includes a third owner whose selected library slot changes while shielding an override'],
  ['learn-refuse-two', 'retains capture and reservations when the second owner refuses, then retries SAVE'],
  ['learn-refuse-three', 'keeps every owner unchanged when the third owner refuses, then retries SAVE'],
  ['disabled', 'retains ordinary preset, modulation, shape and LEARN menu behavior without registration'],
])('%s: %s', (scenario) => {
  expect(() => execFileSync(executable, [scenario], { timeout: 10_000 })).not.toThrow();
});
