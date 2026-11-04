import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { afterAll, beforeAll, expect, test } from 'vitest';
let directory: string, binary: string;
beforeAll(() => {
  directory = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-timing-'));
  binary = path.join(directory, 'timing');
  execFileSync(
    'cc',
    [
      '-std=c11',
      '-Wall',
      '-Wextra',
      '-Werror',
      '-fsanitize=undefined',
      path.join(import.meta.dirname, 'canopy-timing.c'),
      '-o',
      binary,
    ],
    { timeout: 20000 },
  );
});
afterAll(() => {
  if (directory) fs.rmSync(directory, { recursive: true, force: true });
});
const run = (rows: number[][]) =>
  execFileSync(binary, {
    input: rows.map((r) => r.join(' ')).join('\n') + '\n',
    encoding: 'utf8',
    timeout: 2000,
  })
    .trim()
    .split('\n')
    .map(Number);
test('START holds existing identity only until the first committed boundary', () => {
  expect(
    run([
      [0, 0, 100, 1],
      [1, 0, 101, 1],
      [1, 0, 132, 1],
      [1, 1, 133, 1],
      [1, 0, 134, 1],
    ]),
  ).toEqual([0, 1, 1, 0, 0]);
});
test('an absent first boundary expires after 64 blocks without rearming', () => {
  expect(
    run([
      [0, 0, 100, 1],
      [1, 0, 101, 1],
      [1, 0, 165, 1],
      [1, 0, 166, 1],
      [1, 0, 167, 1],
    ]),
  ).toEqual([0, 1, 1, 0, 0]);
});
test('a changed or revoked Sound cannot borrow the startup allowance', () => {
  expect(
    run([
      [0, 0, 1, 1],
      [1, 0, 2, 1],
      [1, 0, 3, 0],
      [1, 0, 4, 1],
    ]),
  ).toEqual([0, 1, 0, 0]);
  expect(
    run([
      [0, 0, 1, 0],
      [1, 0, 2, 0],
      [1, 0, 3, 1],
    ]),
  ).toEqual([0, 0, 0]);
});
test('STOP permits another bounded START without relying on signed counters', () => {
  expect(
    run([
      [1, 0, 4294967290, 1],
      [1, 0, 20, 1],
      [1, 0, 59, 1],
      [0, 0, 60, 1],
      [1, 0, 61, 1],
    ]),
  ).toEqual([1, 1, 0, 0, 1]);
});
