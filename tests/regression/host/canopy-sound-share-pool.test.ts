import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('copy-on-share Sound entries detach or refuse without changing other copies', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-share-pool-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const executable = path.join(directory, 'sound-share-pool');
    execFileSync('cc', [
      '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
      path.join(import.meta.dirname, 'canopy-sound-share-pool.c'),
      path.join(root, 'modules/core/adapters/instrument-play/src/cf/sound-share-pool.c'),
      '-o', executable,
    ], { timeout: 20_000 });
    expect(() => execFileSync(executable, { timeout: 10_000 })).not.toThrow();
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
