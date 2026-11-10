import { afterEach, expect, test, vi } from 'vitest';
import { canopyControls } from '../runtime/canopy-controls.ts';

afterEach(() => vi.useRealTimers());

function fixture() {
  const values = Array.from({ length: 32 }, (_, i) => 10 + i);
  const bank = Array.from({ length: 32 }, (_, i) => (i < 16 ? i : values[i - 16]! | ((values[i] ?? 0) << 16)));
  let phase = 3,
    requested = 9,
    applied = 9,
    pending = 0,
    tunePending = 0;
  const cf = vi.fn(async (name: string, index = 0) => {
    if (name === 'ip_control') return index === 6 ? applied : phase;
    if (index === 3) return requested;
    if (index === 4) return pending;
    if (index === 30) return tunePending;
    const offset = (index - 7) * 2;
    return ((values[offset]! << 16) | values[offset + 1]!) >>> 0;
  });
  const rawBank = vi.fn(async () => bank);
  return {
    values,
    bank,
    cf,
    rawBank,
    tune: (pending: number) => {
      tunePending = pending;
    },
    set: (p: number, r: number, a: number, n: number) => {
      phase = p;
      requested = r;
      applied = a;
      pending = n;
    },
  };
}

test('legacy low halves and WT high halves match their requested even/odd controls', async () => {
  const f = fixture();
  const reader = canopyControls(f.cf, f.rawBank);
  for (const control of [0, 1, 2, 15, 16, 17, 26, 27, 28, 31])
    expect(await reader.settled(control)).toBe(f.values[control]);
});

test('readback waits for routing, applied revision and detailed pending confirmation', async () => {
  vi.useFakeTimers();
  const f = fixture();
  f.set(2, 10, 9, 1);
  const done = canopyControls(f.cf, f.rawBank, { pollMs: 10 }).settled(17);
  await vi.advanceTimersByTimeAsync(40);
  expect(f.rawBank).not.toHaveBeenCalled();
  f.set(3, 10, 9, 1);
  await vi.advanceTimersByTimeAsync(40);
  expect(f.rawBank).not.toHaveBeenCalled();
  f.set(3, 10, 10, 1);
  await vi.advanceTimersByTimeAsync(40);
  expect(f.rawBank).not.toHaveBeenCalled();
  f.set(3, 10, 10, 0);
  await vi.advanceTimersByTimeAsync(10);
  expect(await done).toBe(27);
  expect(f.rawBank).toHaveBeenCalledTimes(1);
});

test('a claimed settled revision with mismatched effective data fails explicitly', async () => {
  const f = fixture();
  f.bank[17] = (99 << 16) | f.values[1]!;
  await expect(canopyControls(f.cf, f.rawBank).settled(17)).rejects.toThrow('settled request 27 differs from DSP 99');
});

test('native tuning must also be acknowledged before a bank is called settled', async () => {
  vi.useFakeTimers();
  const f = fixture();
  f.tune(1);
  const done = canopyControls(f.cf, f.rawBank, { pollMs: 10 }).settled(31);
  await vi.advanceTimersByTimeAsync(30);
  expect(f.rawBank).not.toHaveBeenCalled();
  f.tune(0);
  await vi.advanceTimersByTimeAsync(10);
  expect(await done).toBe(f.values[31]);
});

test('a stuck pending configuration expires without reading a misleading old bank', async () => {
  vi.useFakeTimers();
  const f = fixture();
  f.set(3, 10, 10, 1);
  const done = expect(canopyControls(f.cf, f.rawBank, { timeoutMs: 100, pollMs: 10 }).settled(0)).rejects.toThrow();
  await vi.advanceTimersByTimeAsync(100);
  await done;
  expect(f.rawBank).not.toHaveBeenCalled();
  expect(vi.getTimerCount()).toBe(0);
});

test('native controls and malformed banks are rejected', async () => {
  const f = fixture();
  await expect(canopyControls(f.cf, f.rawBank).settled(32)).rejects.toThrow('Invalid CANOPY custom control');
  expect(f.cf).not.toHaveBeenCalled();
  f.rawBank.mockResolvedValue([]);
  await expect(canopyControls(f.cf, f.rawBank).settled(0)).rejects.toThrow('Invalid CANOPY configuration bank length');
});
