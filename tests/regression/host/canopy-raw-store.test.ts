import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('raw Sound records survive without CANOPY and fail closed on capacity', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-raw-store-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const fixture = path.join(root, 'tests/regression/fixtures/canopy-raw-store');
    const executable = path.join(directory, 'raw-store');
    execFileSync('cc', [
      '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
      path.join(fixture, 'raw-store-test.c'), path.join(fixture, 'raw-store.c'),
      path.join(root, 'modules/core/adapters/instrument-play/src/cf/sound-pool.c'),
      '-o', executable,
    ], { timeout: 20_000 });
    expect(() => execFileSync(executable, { timeout: 10_000 })).not.toThrow();
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
