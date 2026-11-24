import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('USB and DIN claim native routes, pair keys, clean up sustain, and scope selectors', () => {
  const directory = mkdtempSync(join(tmpdir(), 'digikit-track-midi-'));
  const root = resolve(import.meta.dirname, '../../..');
  const module = join(root, 'image-modules/adapters/track-keyboard');
  const binary = join(directory, 'midi');
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
        join(module, 'src/track-midi.c'),
        join(module, 'tests/track-midi.c'),
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
