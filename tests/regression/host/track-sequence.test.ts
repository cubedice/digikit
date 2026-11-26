import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('native due trigs feed bounded phrases and recorder commits retain input timing', () => {
  const directory = mkdtempSync(join(tmpdir(), 'digikit-track-sequence-'));
  const root = resolve(import.meta.dirname, '../../..');
  const module = join(root, 'runtime/platform/track-keyboard');
  const binary = join(directory, 'sequence');
  try {
    execFileSync(
      'cc',
      [
        '-std=c11',
        '-O2',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-fsanitize=undefined',
        ...['track-keyboard', 'track-valid', 'track-config', 'track-service', 'track-sequence', 'track-capture'].map(
          (file) => join(module, 'src', file + '.c'),
        ),
        join(module, 'tests/track-sequence.c'),
        '-o',
        binary,
      ],
      { timeout: 20_000 },
    );
    execFileSync(binary, { timeout: 10_000 });
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
});
