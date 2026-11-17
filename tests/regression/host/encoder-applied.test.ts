import { afterEach, expect, test, vi } from 'vitest';
import { turnAppliedControl } from '../runtime/encoder-applied.ts';

afterEach(() => vi.useRealTimers());

function fixture(initial = 0) {
  let applied = initial;
  const inputTurn = vi.fn(async (_encoder: number, delta: number) => {
    // A slow guest publishes the applied value after the transport has replied.
    setTimeout(() => {
      applied += Math.max(-30, Math.min(30, delta));
    }, 90);
  });
  const read = vi.fn(async () => applied);
  return { session: { client: { inputTurn } }, inputTurn, read, applied: () => applied };
}

test('large positive changes wait for each applied value before sending another bounded turn', async () => {
  vi.useFakeTimers();
  const f = fixture();
  const done = turnAppliedControl(f.session, 3, f.read, 350, { pollMs: 10 });
  await vi.advanceTimersByTimeAsync(80);
  expect(f.inputTurn.mock.calls).toEqual([[3, 30]]);
  expect(f.applied()).toBe(0);
  await vi.advanceTimersByTimeAsync(10);
  expect(f.applied()).toBe(30);
  expect(f.inputTurn.mock.calls).toHaveLength(2);
  await vi.advanceTimersByTimeAsync(990);
  await done;
  expect(f.inputTurn.mock.calls).toEqual([...Array.from({ length: 11 }, () => [3, 30]), [3, 20]]);
  expect(f.applied()).toBe(350);
  expect(vi.getTimerCount()).toBe(0);
});

test('negative changes use the observed starting value and an exact final remainder', async () => {
  vi.useFakeTimers();
  const f = fixture(95);
  const done = turnAppliedControl(f.session, 7, f.read, 12, { pollMs: 10 });
  await vi.advanceTimersByTimeAsync(270);
  await done;
  expect(f.inputTurn.mock.calls).toEqual([
    [7, -30],
    [7, -30],
    [7, -23],
  ]);
  expect(f.applied()).toBe(12);
});

test('an already applied target sends no input', async () => {
  const f = fixture(50);
  await turnAppliedControl(f.session, 4, f.read, 50);
  expect(f.inputTurn).not.toHaveBeenCalled();
});

test('a dropped turn stops at its deadline without sending retries that could accumulate later', async () => {
  vi.useFakeTimers();
  const f = fixture();
  f.inputTurn.mockImplementation(async () => {});
  const failed = expect(turnAppliedControl(f.session, 7, f.read, 64, { timeoutMs: 100, pollMs: 10 })).rejects.toThrow(
    'failed to apply 0 -> 30 toward 64',
  );
  await vi.advanceTimersByTimeAsync(100);
  await failed;
  expect(f.inputTurn.mock.calls).toEqual([[7, 30]]);
  expect(vi.getTimerCount()).toBe(0);
});

test('unexpected applied progress fails instead of concealing a routing or scaling defect', async () => {
  const f = fixture();
  f.inputTurn.mockImplementation(async () => {});
  f.read.mockResolvedValueOnce(0).mockResolvedValue(29);
  await expect(turnAppliedControl(f.session, 3, f.read, 350)).rejects.toMatchObject({
    message: 'Encoder 3: failed to apply 0 -> 30 toward 350',
    cause: { message: 'Unexpected applied value 29' },
  });
  expect(f.inputTurn).toHaveBeenCalledTimes(1);
});

test.each(['read', 'turn'] as const)('a hung %s RPC cannot evade the step deadline', async (phase) => {
  vi.useFakeTimers();
  const f = fixture();
  if (phase === 'read') f.read.mockImplementation(() => new Promise<number>(() => {}));
  else f.inputTurn.mockImplementation(() => new Promise<void>(() => {}));
  const failed = expect(turnAppliedControl(f.session, 3, f.read, 350, { timeoutMs: 100 })).rejects.toThrow();
  await vi.advanceTimersByTimeAsync(100);
  await failed;
  expect(vi.getTimerCount()).toBe(0);
});

test('invalid targets and readbacks fail before sending input', async () => {
  const f = fixture();
  await expect(turnAppliedControl(f.session, 3, f.read, 1.5)).rejects.toThrow('Invalid applied control target');
  await expect(turnAppliedControl(f.session, 0, f.read, 50)).rejects.toThrow('Invalid panel encoder');
  f.read.mockResolvedValue(Number.NaN);
  await expect(turnAppliedControl(f.session, 3, f.read, 50)).rejects.toThrow('invalid applied value');
  expect(f.inputTurn).not.toHaveBeenCalled();
});
