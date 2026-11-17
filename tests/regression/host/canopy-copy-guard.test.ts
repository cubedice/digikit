import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('Sound and kit copies defer MIDI selector edits at each copy chunk', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-copy-guard-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const source = path.join(root, 'modules/core/adapters/instrument-play/src/cf');
    const fixture = path.join(root, 'tests/regression/fixtures/canopy-copy-guard');
    const executable = path.join(directory, 'copy-guard');
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
        path.join(import.meta.dirname, 'canopy-copy-guard.c'),
        path.join(root, 'tests/regression/fixtures/canopy_keyboard_proof.c'),
        ...[
          'preset.c',
          'src-state.c',
          'src-lock-packed.c',
          'status.c',
          'src-display.c',
          'sound-codec.c',
          'sound-share-pool.c',
        ].map((f) => path.join(source, f)),
        path.join(fixture, 'copy-guard.c'),
        '-o',
        executable,
      ],
      { timeout: 20_000 },
    );
    expect(() => execFileSync(executable, { timeout: 20_000 })).not.toThrow();
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
  // the compile with the sanitizer takes over 5 s beside a running gate: the limit is the two bounds above
}, 45_000);
