import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { beforeAll, afterAll, expect, test } from 'vitest';
let dir: string, binary: string, panelBinary: string;
beforeAll(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-native-'));
  binary = path.join(dir, 'test');
  panelBinary = path.join(dir, 'panel');
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
      ...['src-state.c', 'src-display.c', 'src-lock-packed.c', 'status.c', 'preset.c'].map((p) => path.join(src, p)),
      path.join(root, 'modules/core/adapters/machines/src/cf/dt2/src-pages.c'),
      '-o',
      binary,
    ],
    { timeout: 20000 },
  );
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
      path.join(import.meta.dirname, 'canopy-panel.c'),
      '-o',
      panelBinary,
    ],
    { timeout: 20000 },
  );
});
test('resolved panel bridge consumes only CANOPY and releases ownership on view changes', () => {
  expect(execFileSync(panelBinary, { encoding: 'utf8', timeout: 2000 }).trim()).toBe('ok');
});
afterAll(() => {
  if (dir) fs.rmSync(dir, { recursive: true, force: true });
});
test.each([
  'held-lock',
  'lock-paint',
  'live-lock',
  'paint',
  'diagrams',
  'tone',
  'wavetable-fm',
  'fm-envelope',
  'fm-presentation',
  'routing',
  'values',
  'delayed',
  'overflow',
  'alternate',
  'attach',
  'tick',
  'track',
])('native SRC: %s', (scenario) => {
  expect(execFileSync(binary, [scenario], { encoding: 'utf8', timeout: 2000 }).trim()).toBe('ok');
});
