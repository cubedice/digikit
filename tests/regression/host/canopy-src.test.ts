import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

let directory: string, binary: string;
beforeAll(() => {
  directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-src-'));
  binary = path.join(directory, 'src');
  const source = path.resolve(import.meta.dirname, '../../../runtime/platform/instrument-play/src/cf');
  execFileSync(
    'cc',
    [
      '-std=c11',
      '-Wall',
      '-Wextra',
      '-Werror',
      '-fsanitize=undefined',
      path.join(import.meta.dirname, 'canopy-src.c'),
      path.join(source, 'src-state.c'),
      path.join(source, 'preset.c'),
      path.join(source, 'status.c'),
      path.join(source, 'src-display.c'),
      path.join(source, 'src-lock-packed.c'),
      path.resolve(import.meta.dirname, '../../../runtime/platform/keyboard-panel/src/cf/src-clock.c'),
      '-o',
      binary,
    ],
    { timeout: 20_000 },
  );
});
afterAll(() => {
  if (directory) fs.rmSync(directory, { recursive: true, force: true });
});
test.each([
  'controls',
  'wavetable-clock',
  'shared-keyboard-layout',
  'pattern-tempo-import',
  'trig-lock-edit',
  'lock-indicators',
  'wavetable',
  'wavetable-status',
  'wavetable-fm',
  'wavetable-fm-status',
  'list-small-turns',
  'list-isolation',
  'list-context',
  'choice-wave',
  'fm-controls',
  'fm-import',
  'fm-status',
  'stale',
  'readonly',
  'clamp',
  'focus',
  'applied',
  'metadata',
  'native-tune',
  'freshness',
  'poll-freshness',
  'poll-loss',
  'status-wait',
  'pitch-zero',
  'malformed',
  'details',
  'fragment-validation',
  'fragment-framing',
  'timeout',
  'learn-identity',
  'wrap',
  'display-notes',
  'display-clock',
  'wall-clock',
  'display-units',
  'display-capacity',
  'duplicate-tone',
  'display-focus',
  'native-presentation',
])('SRC controls and telemetry: %s', (scenario) => {
  expect(execFileSync(binary, [scenario], { encoding: 'utf8', timeout: 2000 }).trim()).toBe('ok');
});
