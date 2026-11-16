import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-menu-transaction-'));
const executable = path.join(directory, 'menu-transaction');

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
      path.join(import.meta.dirname, 'canopy-menu-transaction.c'),
      ...[
        'keyboard.c',
        'preset.c',
        'src-state.c',
        'src-lock-packed.c',
        'status.c',
        'src-display.c',
        'sound-codec.c',
        'sound-share-pool.c',
      ].map((file) => path.join(source, file)),
      '-o',
      executable,
    ],
    { timeout: 20_000 },
  );
});

afterAll(() => fs.rmSync(directory, { recursive: true, force: true }));

test.each([
  ['clock', 'commits scan clock and its default rate together while preserving copied snapshots'],
  ['inactive', 'edits explicit WT/FM/WTFM banks independently of the selected sound family'],
  ['capacity', 'refuses every setter on exhausted detach capacity without changing sound or menu staging'],
  ['prepared', 'refuses after prepare without partial edits or consumed reservations, then retries'],
  ['invalid', 'rejects out-of-range values, stale owner generations, and active loads before prepare'],
  ['reentry', 'rejects callback re-entry without changing the outer prospective preset'],
])('%s: %s', (scenario) => {
  expect(() => execFileSync(executable, [scenario], { timeout: 10_000 })).not.toThrow();
});
