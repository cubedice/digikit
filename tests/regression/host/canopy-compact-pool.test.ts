import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('compact immutable raw pool reserves atomically and never changes published entries', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-compact-pool-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const fixture = path.join(root, 'tests/regression/fixtures/canopy-raw-store');
    const executable = path.join(directory, 'compact-pool');
    execFileSync('cc', [
      '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
      path.join(fixture, 'compact-pool-test.c'), path.join(fixture, 'compact-pool.c'),
      '-o', executable,
    ], { timeout: 20_000 });
    expect(() => execFileSync(executable, { timeout: 10_000 })).not.toThrow();
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
