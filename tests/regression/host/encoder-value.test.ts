import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { encoderValue } from '../runtime/encoder-value.ts';

beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(0);
});
afterEach(() => vi.useRealTimers());

test('a consumed but filtered first packet is retried, and exact targets never overshoot', async () => {
  let value = 0,
    filtered = true;
  const turn = vi.fn(async (delta: number) => {
    if (filtered) filtered = false;
    else value += delta;
  });
  const read = async () => value;
  for (const target of [10, 9, 10, 11]) {
    await encoderValue(turn, read, target);
    expect(value).toBe(target);
  }
  expect(turn).toHaveBeenCalledTimes(14);
  expect(turn.mock.calls.every(([delta]) => Math.abs(delta) === 1)).toBe(true);
});

test('no second packet is sent while either native handling or DSP configuration is outstanding', async () => {
  let value = 0,
    pending: Promise<void> | undefined;
  const turn = vi.fn(async (delta: number) => {
    await new Promise<void>((resolve) => setTimeout(resolve, 200));
    pending = new Promise<void>((resolve) =>
      setTimeout(() => {
        value += delta;
        resolve();
      }, 800),
    );
  });
  const read = async () => {
    await pending;
    return value;
  };
  const done = encoderValue(turn, read, 2);
  await vi.advanceTimersByTimeAsync(900);
  expect(turn).toHaveBeenCalledTimes(1);
  expect(value).toBe(0);
  await vi.advanceTimersByTimeAsync(100);
  expect(turn).toHaveBeenCalledTimes(2);
  await vi.advanceTimersByTimeAsync(1000);
  await done;
  expect(value).toBe(2);
  expect(turn).toHaveBeenCalledTimes(2);
  expect(vi.getTimerCount()).toBe(0);
});

test('permanently filtered input stops at the packet bound', async () => {
  const turn = vi.fn(async () => {});
  await expect(encoderValue(turn, async () => 0, 1, { maxTurns: 3 })).rejects.toThrow('in 3 turns');
  expect(turn).toHaveBeenCalledTimes(3);
});

test('a stuck configuration times out without sending a replacement packet', async () => {
  let reads = 0;
  const turn = vi.fn(async () => {});
  const read = async () => (++reads === 1 ? 0 : new Promise<number>(() => {}));
  const done = expect(encoderValue(turn, read, 1, { timeoutMs: 300 })).rejects.toThrow();
  await vi.advanceTimersByTimeAsync(300);
  await done;
  expect(turn).toHaveBeenCalledTimes(1);
  expect(vi.getTimerCount()).toBe(0);
});

test('unexpected acceleration fails instead of oscillating around the target', async () => {
  let value = 0;
  const turn = vi.fn(async () => {
    value += 2;
  });
  await expect(encoderValue(turn, async () => value, 1)).rejects.toThrow('moved unexpectedly');
  expect(turn).toHaveBeenCalledTimes(1);
});
