import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('the loop controls: the crossfader, the counters, the held values and what a new loop does', () => {
  const temp = mkdtempSync(join(tmpdir(), 'dk-loop-control-'));
  try {
    const executable = join(temp, 'control');
    execFileSync(
      'cc',
      [
        '-std=c11',
        '-O2',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-I',
        resolve('sdk/include'),
        resolve('modules/core/adapters/loop-control/tests/control_harness.c'),
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
