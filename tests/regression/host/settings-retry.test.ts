import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';

let directory: string, binary: string;
beforeAll(() => {
  directory = fs.mkdtempSync(path.join(os.tmpdir(), 'settings-retry-'));
  const root = path.resolve(import.meta.dirname, '../../..');
  const library = path.join(directory, 'libdkrt_sdk.rlib');
  binary = path.join(directory, 'retry');
  execFileSync(
    'rustc',
    [
      '--edition=2021',
      '--crate-name',
      'dkrt_sdk',
      '--crate-type',
      'rlib',
      path.join(root, 'modules/core/runtime/sdk/src/lib.rs'),
      '-o',
      library,
    ],
    { timeout: 20_000 },
  );
  execFileSync(
    'rustc',
    [
      '--edition=2021',
      path.join(import.meta.dirname, 'settings-retry.rs'),
      '--extern',
      `dkrt_sdk=${library}`,
      '-o',
      binary,
    ],
    { timeout: 20_000 },
  );
});
afterAll(() => {
  if (directory) fs.rmSync(directory, { recursive: true, force: true });
});
test.each(['readonly', 'range', 'exhausted'])('explicit save retries after %s stopped automatic writes', (error) => {
  const s = JSON.parse(execFileSync(binary, [error], { encoding: 'utf8', timeout: 2000 }));
  expect(s.failure).toBeLessThan(0);
  expect(s).toMatchObject({
    first: 0,
    dirty: 0,
    retry: 0,
    newWrites: 1,
    clean: 0,
    cleanWrites: 0,
    length: 3,
    value: 'new',
  });
});
