import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('the phrase page: encoders, presses, SYNC notes, the cells and what is drawn', () => {
  const temp = mkdtempSync(join(tmpdir(), 'dk-phrase-ui-'));
  try {
    const executable = join(temp, 'ui');
    const module = resolve('modules/core/adapters/phrase-ui');
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
        join(module, 'tests/ui_harness.c'),
        join(module, 'src/model.c'),
        join(module, 'src/render.c'),
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
