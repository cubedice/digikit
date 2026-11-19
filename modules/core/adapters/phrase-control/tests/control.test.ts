import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('phrase controls, the SYNC note table, persistence and status assembly', () => {
  const temp = mkdtempSync(join(tmpdir(), 'dk-phrase-control-'));
  try {
    const executable = join(temp, 'control');
    execFileSync(
      'cc',
      [
        '-std=c11',
        '-O2',
        '-Wno-int-to-pointer-cast',
        '-I',
        resolve('sdk/include'),
        resolve('modules/core/adapters/phrase-control/tests/control_harness.c'),
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
