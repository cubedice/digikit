import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('task refill keeps credits across owner and MIDI changes during allocation', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-credit-refill-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const source = path.join(root, 'modules/core/adapters/instrument-play/src/cf');
    const twoPhase = path.join(root, 'tests/regression/fixtures/canopy-two-phase');
    const executable = path.join(directory, 'credit-refill');
    execFileSync('cc', [
      '-std=c11', '-DDK_CANOPY_SOUND_PROOF=1', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
      '-I', path.join(root, 'sdk/include'),
      path.join(import.meta.dirname, 'canopy-credit-refill.c'),
      path.join(root, 'tests/regression/fixtures/canopy_keyboard_proof.c'),
      path.join(root, 'tests/regression/fixtures/canopy-credits/boundary-credits.c'),
      path.join(twoPhase, 'two-phase.c'), path.join(twoPhase, 'credit-refill.c'),
      ...['preset.c', 'src-state.c', 'src-lock-packed.c', 'status.c', 'src-display.c', 'sound-codec.c', 'sound-pool.c'].map((f) => path.join(source, f)),
      '-o', executable,
    ], { timeout: 20_000 });
    expect(() => execFileSync(executable, { timeout: 10_000 })).not.toThrow();
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
