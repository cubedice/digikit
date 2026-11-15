import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { expect, test } from 'vitest';

test('project-load preflight reserves all storage before publishing snapshots', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-load-preflight-'));
  try {
    const root = path.resolve(import.meta.dirname, '../../..');
    const proof = path.join(root, 'tests/regression/fixtures/canopy-load-preflight');
    const executable = path.join(directory, 'preflight');
    execFileSync('cc', [
      '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
      path.join(proof, 'preflight-test.c'), path.join(proof, 'preflight.c'),
      path.join(root, 'modules/core/adapters/instrument-play/src/cf/sound-pool.c'),
      '-o', executable,
    ], { timeout: 20_000 });
    expect(() => execFileSync(executable, { timeout: 10_000 })).not.toThrow();
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
