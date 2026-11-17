import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('CANOPY Sound extensions follow stock copies, loads and clears outside Sound memory', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-sound-assoc-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const cf = path.join(root, 'modules/core/adapters/instrument-play/src/cf');
    const executable = path.join(directory, 'sound-assoc');
    execFileSync(
      'cc',
      [
        '-std=c11',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-fsanitize=undefined,address',
        path.join(import.meta.dirname, 'canopy-sound-assoc.c'),
        path.join(cf, 'sound-assoc.c'),
        path.join(cf, 'sound-record.c'),
        path.join(cf, 'preset.c'),
        '-o',
        executable,
      ],
      { timeout: 30_000 },
    );
    expect(() => execFileSync(executable, { timeout: 30_000 })).not.toThrow();
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
