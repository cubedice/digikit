import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('master scale changes commit with their audible nominal boundary', () => {
  const temp = mkdtempSync(join(tmpdir(), 'dk-phrase-clock-'));
  try {
    const executable = join(temp, 'clock');
    execFileSync(
      'cc',
      [
        '-std=c11',
        '-O2',
        '-Wall',
        '-Wextra',
        '-Werror',
        resolve('modules/core/adapters/pattern-position/tests/clock_harness.c'),
        '-o',
        executable,
      ],
      { stdio: 'pipe' },
    );
    execFileSync(executable, [], { stdio: 'pipe' });
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
});
