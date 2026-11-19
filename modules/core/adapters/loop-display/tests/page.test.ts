import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('the loop page: the crossfader, ARM as one press, the drop, the texts of every state and what is drawn', () => {
  const temp = mkdtempSync(join(tmpdir(), 'dk-loop-page-'));
  try {
    const executable = join(temp, 'page');
    const module = resolve('modules/core/adapters/loop-display');
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
        join(module, 'tests/page_harness.c'),
        join(module, 'src/model.c'),
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
