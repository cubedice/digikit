import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-live-ui-transaction-'));
const executable = path.join(directory, 'live-ui-transaction');

beforeAll(() => {
  const root = path.resolve(import.meta.dirname, '../../..');
  const source = path.join(root, 'runtime/platform/instrument-play/src/cf');
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
      path.join(import.meta.dirname, 'canopy-live-ui-transaction.c'),
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
  ['preset', 'loads through the real preset row and retains pending saves on refusal'],
  ['modulation', 'commits scan clock/rate pairs and restores signed menu amounts on reservation refusal'],
  ['inactive', 'addresses inactive wheel banks and retains reservations after failed preparation'],
  ['disabled', 'retains ordinary preset and modulation menu behavior without registration'],
])('%s: %s', (scenario) => {
  expect(() => execFileSync(executable, [scenario], { timeout: 10_000 })).not.toThrow();
});
