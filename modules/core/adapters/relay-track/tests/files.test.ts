import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { test } from 'vitest';

test('track tape waveform caching, FULL, HITS, and I/O recovery', () => {
  const tmp = mkdtempSync(join(tmpdir(), 'dk-relay-files-'));
  try {
    const exe = join(tmp, 'files');
    const root = resolve('modules/core/adapters/relay-track');
    execFileSync('cc', [
      '-std=c11',
      '-O2',
      '-Wall',
      '-Wextra',
      '-Werror',
      '-Imodules/core/fw/include',
      join(root, 'src/files.c'),
      join(root, 'tests/files_harness.c'),
      '-o',
      exe,
    ]);
    execFileSync(exe);
  } finally {
    rmSync(tmp, { recursive: true, force: true });
  }
});
