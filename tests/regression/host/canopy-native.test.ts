import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { beforeAll, afterAll, expect, test } from 'vitest';
let dir: string, binary: string;
beforeAll(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-native-'));
  binary = path.join(dir, 'test');
  const root = path.resolve(import.meta.dirname, '../../..');
  const src = path.join(root, 'modules/core/adapters/instrument-play/src/cf');
  execFileSync(
    'cc',
    [
      '-std=c11',
      '-Wall',
      '-Wextra',
      '-Werror',
      '-fsanitize=undefined',
      '-fno-pie',
      '-no-pie',
      '-I' + path.join(root, 'modules/core/fw/include'),
      path.join(import.meta.dirname, 'canopy-native.c'),
      ...['src-state.c', 'src-display.c', 'status.c'].map((p) => path.join(src, p)),
      path.join(root, 'modules/core/adapters/machines/src/cf/dt2/src-pages.c'),
      '-o',
      binary,
    ],
    { timeout: 20000 },
  );
});
afterAll(() => {
  if (dir) fs.rmSync(dir, { recursive: true, force: true });
});
test.each(['paint', 'routing', 'delayed', 'overflow', 'alternate', 'attach', 'tick', 'track'])(
  'native SRC: %s',
  (scenario) => {
    expect(execFileSync(binary, [scenario], { encoding: 'utf8', timeout: 2000 }).trim()).toBe('ok');
  },
);
