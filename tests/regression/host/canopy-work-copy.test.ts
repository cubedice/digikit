import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('the CANOPY working copy round-trips and applies a boot copy once', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-work-copy-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const executable = path.join(directory, 'work-copy');
    execFileSync(
      'cc',
      [
        '-std=c11',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-fsanitize=undefined',
        '-I',
        path.join(root, 'sdk/include'),
        path.join(import.meta.dirname, 'canopy-work-copy.c'),
        '-o',
        executable,
      ],
      { timeout: 20_000 },
    );
    expect(() => execFileSync(executable, { timeout: 10_000 })).not.toThrow();
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
