import { afterEach, expect, test, vi } from 'vitest';
import { menuSelection } from '../runtime/menu-selection.ts';
import type { RuntimePlan } from '../runtime/runtime-plan.ts';

afterEach(() => vi.useRealTimers());

function fixture(selected = 18) {
  const pointer = 0x48000000,
    menu = 0x48001000,
    object = 0x48002000;
  const memory = new Map<number, number>([
    [pointer, menu],
    [menu + 4, object],
    [object + 4, 0x48003000],
    [object + 8, 0x48003000 + 38 * 8],
    [object + 0x10, selected],
    [object + 0x64, 0x48004000],
    [object + 0x68, 0],
  ]);
  const plan = {
    symbols: {
      'core/instrument-play@0.1.0:menu': pointer,
      'core/other@0.1.0:menu': 0x49000000,
    },
  } as unknown as RuntimePlan;
  const memPeek = vi.fn(async (address: number) => memory.get(address) ?? null);
  const tap = vi.fn(async (name: string) => {
    const current = memory.get(object + 0x10)!;
    memory.set(object + 0x10, current + (name === 'DOWN' ? 1 : -1));
  });
  const select = menuSelection({ client: { memPeek } }, plan, 'core/instrument-play', tap);
  return { select, tap, memory, memPeek, pointer, menu, object };
}

test('moves directly from observed selection using real keys and verifies the final row', async () => {
  const f = fixture(18);
  await f.select('menu', 13);
  expect(f.tap.mock.calls).toEqual(Array.from({ length: 5 }, () => ['UP']));
  expect(f.memory.get(f.object + 0x10)).toBe(13);
  f.tap.mockClear();
  await f.select('menu', 15);
  expect(f.tap.mock.calls).toEqual([['DOWN'], ['DOWN']]);
});

test('an already selected target sends no navigation and ignores another owner with the same symbol', async () => {
  const f = fixture(34);
  await f.select('menu', 34);
  expect(f.tap).not.toHaveBeenCalled();
  expect(f.memPeek.mock.calls.some(([address]) => address === 0x49000000)).toBe(false);
});

test('a dropped or intercepted key fails at the first unexpected row', async () => {
  const f = fixture(6);
  f.tap.mockImplementation(async () => {});
  await expect(f.select('menu', 18)).rejects.toThrow('expected row 7, observed 6');
  expect(f.tap).toHaveBeenCalledTimes(1);
});

test.each([0, 0x48004000])('rejects a closed menu before sending keys (manager %i)', async (manager) => {
  const f = fixture();
  f.memory.set(f.object + 0x64, manager);
  if (manager) f.memory.set(f.object + 0x68, 0x01000000);
  await expect(f.select('menu', 13)).rejects.toThrow('is not open');
  expect(f.tap).not.toHaveBeenCalled();
});

test('refuses out-of-range targets and malformed item vectors without navigation', async () => {
  const f = fixture();
  await expect(f.select('menu', 38)).rejects.toThrow('row 38 is outside 0..37');
  f.memory.set(f.object + 8, 0x48003001);
  await expect(f.select('menu', 13)).rejects.toThrow('invalid rows');
  expect(f.tap).not.toHaveBeenCalled();
});

test('reports a missing named menu rather than guessing another menu address', async () => {
  const f = fixture();
  await expect(f.select('preset_menu', 2)).rejects.toThrow('symbol preset_menu: 0 addresses');
  expect(f.tap).not.toHaveBeenCalled();
});

test('a hung menu read has a bounded deadline', async () => {
  vi.useFakeTimers();
  const f = fixture();
  f.memPeek.mockImplementation(() => new Promise<number | null>(() => {}));
  const failed = expect(f.select('menu', 13)).rejects.toThrow();
  await vi.advanceTimersByTimeAsync(30_000);
  await failed;
  expect(f.tap).not.toHaveBeenCalled();
  expect(vi.getTimerCount()).toBe(0);
});
