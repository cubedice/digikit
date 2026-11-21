import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'vitest';

test('stock Sound saves publish completed sample references and recall controls after loading', () => {
  const temp = mkdtempSync(join(tmpdir(), 'relay-sound-'));
  try {
    const exe = join(temp, 'sound');
    execFileSync('cc', [
      '-std=c11',
      '-O2',
      '-ffunction-sections',
      '-fdata-sections',
      '-Wl,--gc-sections',
      '-no-pie',
      '-Isdk/include',
      '-Imodules/core/fw/include',
      'modules/core/adapters/relay-track/tests/sound_harness.c',
      '-o',
      exe,
    ]);
    execFileSync(exe);
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
});
