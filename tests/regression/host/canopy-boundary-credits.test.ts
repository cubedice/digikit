import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('task credits reserve atomically and real SRC edits fail closed', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-boundary-credits-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const source = path.join(root, 'modules/core/adapters/instrument-play/src/cf');
    const executable = path.join(directory, 'boundary-credits');
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
        path.join(import.meta.dirname, 'canopy-boundary-credits.c'),
        path.join(root, 'tests/regression/fixtures/canopy_keyboard_proof.c'),
        path.join(root, 'tests/regression/fixtures/canopy-credits/boundary-credits.c'),
        ...[
          'preset.c',
          'src-state.c',
          'src-lock-packed.c',
          'status.c',
          'src-display.c',
          'sound-codec.c',
          'sound-pool.c',
        ].map((f) => path.join(source, f)),
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
