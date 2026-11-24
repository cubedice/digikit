import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('tracks own chord notes independently and schedule direct and arpeggiated playback', () => {
  const directory = mkdtempSync(join(tmpdir(), 'digikit-track-keyboard-'));
  const root = resolve(import.meta.dirname, '../../..');
  const module = join(root, 'image-modules/adapters/track-keyboard');
  const binary = join(directory, 'keys');
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
        join(module, 'src/track-keyboard.c'),
        join(module, 'src/track-valid.c'),
        join(module, 'src/track-config.c'),
        join(module, 'src/track-service.c'),
        join(module, 'src/track-pattern.c'),
        join(module, 'src/track-record.c'),
        join(module, 'src/track-native.c'),
        join(module, 'src/track-pool.c'),
        join(module, 'src/track-pool-fw.c'),
        join(module, 'tests/track-keyboard.c'),
        join(module, 'tests/track-native.c'),
        join(module, 'tests/track-pool.c'),
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

test('native One Shot chords can reattack their helpers through sample tails', () => {
  const directory = mkdtempSync(join(tmpdir(), 'digikit-track-pool-admission-'));
  const root = resolve(import.meta.dirname, '../../..');
  const module = join(root, 'image-modules/adapters/track-keyboard');
  const binary = join(directory, 'pool-admission');
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
        '-I',
        join(root, 'runtime/core/include'),
        '-I',
        join(root, 'sdk/include'),
        join(module, 'src/track-keyboard.c'),
        join(module, 'src/track-valid.c'),
        join(module, 'src/track-config.c'),
        join(module, 'src/track-native.c'),
        join(module, 'src/track-pool.c'),
        join(module, 'src/track-pool-fw.c'),
        join(module, 'tests/track-pool-admission.c'),
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
