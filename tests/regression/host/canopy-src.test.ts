import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

let directory: string, binary: string;
beforeAll(() => {
  directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-src-'));
  binary = path.join(directory, 'src');
  const source = path.resolve(import.meta.dirname, '../../../modules/core/adapters/instrument-play/src/cf');
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
      path.join(source, 'status.c'),
      path.join(source, 'src-display.c'),
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
  'stale',
  'readonly',
  'clamp',
  'focus',
  'applied',
  'freshness',
  'pitch-zero',
  'malformed',
  'details',
  'fragment-validation',
  'fragment-framing',
  'timeout',
  'learn-identity',
  'wrap',
  'display-notes',
  'duplicate-tone',
  'display-focus',
  'pixels',
])('SRC controls and telemetry: %s', (scenario) => {
  expect(execFileSync(binary, [scenario], { encoding: 'utf8', timeout: 2000 }).trim()).toBe('ok');
});
