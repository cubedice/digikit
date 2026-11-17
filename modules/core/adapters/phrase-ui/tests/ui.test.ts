import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('phrase UI owns gestures, recalls pages, locks capture input and bounds rendering', () => {
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
