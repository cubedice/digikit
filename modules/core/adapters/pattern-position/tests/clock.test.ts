import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

/** Compile one harness of the boundary queue (src/cf/boundary.c with PP_QUEUE_TEST) for the host and run it. */
function runHarness(name: string): void {
  const temp = mkdtempSync(join(tmpdir(), 'dk-phrase-clock-'));
  try {
    const executable = join(temp, name);
    execFileSync(
      'cc',
      [
        '-std=c11',
        '-O2',
        '-Wall',
        '-Wextra',
        '-Werror',
        resolve(`modules/core/adapters/pattern-position/tests/${name}.c`),
        '-o',
        executable,
      ],
      { stdio: 'pipe' },
    );
    execFileSync(executable, [], { stdio: 'pipe' });
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
}

test('master scale changes commit with their audible nominal boundary', () => {
  runHarness('clock_harness');
});

test('a boundary carries the playing pattern and its length, taken at master step edges', () => {
  runHarness('playing_harness');
});
