import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'vitest';

test('TRIG views keep stock controls separate from custom edits and held-step locks', () => {
  const temp = mkdtempSync(join(tmpdir(), 'digikit-track-keyboard-trig-'));
  try {
    const module = 'runtime/platform/track-keyboard';
    const binary = join(temp, 'trig');
    execFileSync(
      'cc',
      [
        '-std=c11',
        '-O2',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-fsanitize=undefined',
        '-no-pie',
        '-Iports/bindings/include',
        join(module, 'src/track-keyboard.c'),
        join(module, 'src/track-valid.c'),
        join(module, 'src/track-config.c'),
        join(module, 'src/trig-view.c'),
        join(module, 'src/image.c'),
        join(module, 'tests/trig-view.c'),
        '-o',
        binary,
      ],
      { timeout: 20_000 },
    );
    execFileSync(binary, { timeout: 10_000 });
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
}, 30_000);
