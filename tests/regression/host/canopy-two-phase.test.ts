import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('task slab staging permits IRQ publication and commits reservations atomically', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-two-phase-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const fixture = path.join(root, 'tests/regression/fixtures/canopy-two-phase');
    const executable = path.join(directory, 'two-phase');
    execFileSync('cc', [
      '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
      path.join(import.meta.dirname, 'canopy-two-phase.c'),
      path.join(fixture, 'two-phase.c'),
      path.join(root, 'modules/core/adapters/instrument-play/src/cf/sound-pool.c'),
      '-o', executable,
    ], { timeout: 20_000 });
    expect(() => execFileSync(executable, { timeout: 10_000 })).not.toThrow();
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
