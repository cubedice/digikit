import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-learn-transaction-'));
const executable = path.join(directory, 'learn-transaction');

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
      path.join(import.meta.dirname, 'canopy-learn-transaction.c'),
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
  ['mixed', 'commits real LEARN for source and non-USER peers, respecting overrides and preserving copies'],
  ['exhausted', 'refuses exhausted later-owner reservations without changing any live state or earlier prepare'],
  ['retry', 'retains the capture and reservations on refusal, then commits every owner together'],
  ['alias', 'rejects one reservation promised to two frozen owners before mutation'],
  ['stale', 'rejects a stale displayed capture without mutation and accepts the refreshed capture'],
  ['owner', 'rejects an affected owner replaced at the same Sound address before any commit'],
  ['reentry', 'rejects callback re-entry without corrupting the outer transaction scratch'],
  ['divergent', 'prepares a divergent peer using another slot copied by the real shared-library save'],
])('%s: %s', (scenario) => {
  expect(() => execFileSync(executable, [scenario], { timeout: 10_000 })).not.toThrow();
});
