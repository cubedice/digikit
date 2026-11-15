import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-shape-slot-transaction-'));
const executable = path.join(directory, 'shape-slot-transaction');

beforeAll(() => {
  const root = path.resolve(import.meta.dirname, '../../..');
  const source = path.join(root, 'modules/core/adapters/instrument-play/src/cf');
  execFileSync('cc', [
    '-std=c11', '-DDK_CANOPY_SOUND_PROOF=1', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
    '-I', path.join(root, 'sdk/include'),
    path.join(import.meta.dirname, 'canopy-shape-slot-transaction.c'),
    ...[
      'keyboard.c', 'preset.c', 'src-state.c', 'src-lock-packed.c', 'status.c',
      'src-display.c', 'sound-codec.c', 'sound-share-pool.c',
    ].map((file) => path.join(source, file)),
    '-o', executable,
  ], { timeout: 20_000 });
});

afterAll(() => fs.rmSync(directory, { recursive: true, force: true }));

test.each([
  ['change', 'changes the library slot, releases the preset override, and preserves prior copies'],
  ['same', 'keeps the preset override when the same slot is selected again'],
  ['capacity', 'restores signed UI backing and refuses reset/routing side effects without capacity'],
  ['prepared', 'leaves state and reservation unchanged after prepare refusal, then retries'],
  ['stale', 'restores invalid or stale staging without overwriting a newer queued selection'],
  ['backing', 'previews the real callback mode/voicing fields while preserving the mapper chord'],
  ['reentry', 'rejects re-entry without corrupting the outer shape snapshot'],
])('%s: %s', (scenario) => {
  expect(() => execFileSync(executable, [scenario], { timeout: 10_000 })).not.toThrow();
});
