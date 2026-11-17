import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('packed locks round-trip every supported parameter and value without colliding with an empty lane', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-packed-lock-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const executable = path.join(directory, 'packed-lock');
    execFileSync(
      'cc',
      [
        '-std=c11',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-fsanitize=undefined',
        path.join(import.meta.dirname, 'canopy-packed-lock.c'),
        path.join(root, 'modules/core/adapters/instrument-play/src/cf/src-lock-packed.c'),
        path.join(root, 'modules/core/adapters/instrument-play/src/cf/src-lock-event.c'),
        path.join(root, 'modules/core/adapters/instrument-play/src/cf/src-lock-apply.c'),
        path.join(root, 'modules/core/adapters/instrument-play/src/cf/src-lock-capture.c'),
        path.join(root, 'modules/core/adapters/instrument-play/src/cf/keyboard.c'),
        path.join(root, 'modules/core/adapters/midi-events/src/cf/ownership.c'),
        path.join(root, 'modules/core/adapters/midi-events/src/cf/transport.c'),
        path.join(root, 'modules/core/adapters/midi-events/src/cf/lock-wire.c'),
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
