import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { waitInstrumentReady, type InstrumentLoadProgress } from '../runtime/instrument-ready.ts';

beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(0);
});
afterEach(() => vi.useRealTimers());

function snapshot(phase = 2): InstrumentLoadProgress {
  return { file: [phase, 0, -35, 2], link: Array<number>(24).fill(0) };
}

function reader(sample: () => InstrumentLoadProgress) {
  return async (symbol: string, index: number) => {
    const state = sample();
    return (symbol === 'DKRT_INSTRUMENT_FILE' ? state.file : state.link)[index]!;
  };
}

test('publication completes immediately with all transport diagnostics', async () => {
  const state = snapshot(3);
  state.file[2] = 0;
  state.link[18] = 840;
  await expect(waitInstrumentReady(reader(() => state))).resolves.toEqual(state);
  expect(vi.getTimerCount()).toBe(0);
});

test.each([6, 9])('terminal phase %i immediately reports file and all 24 link words', async (phase) => {
  const state = snapshot(phase);
  state.file[2] = -35 >>> 0;
  state.link = Array.from({ length: 24 }, (_, i) => 100 + i);
  await expect(waitInstrumentReady(reader(() => state))).rejects.toThrow(JSON.stringify(state));
  expect(Date.now()).toBe(0);
  expect(vi.getTimerCount()).toBe(0);
});

test('an unchanged pending transfer fails at its no-progress deadline', async () => {
  const state = snapshot();
  const failed = expect(waitInstrumentReady(reader(() => state))).rejects.toThrow(
    `Instrument load made no progress for 30000 ms: ${JSON.stringify(state)}`,
  );
  await vi.advanceTimersByTimeAsync(30_000);
  await failed;
  expect(vi.getTimerCount()).toBe(0);
});

test.each(['phase', 'serial', 'acked', 'ending'] as const)(
  '%s progress keeps a slow transfer alive beyond one watchdog interval',
  async (field) => {
    const read = reader(() => {
      const step = Math.floor(Date.now() / 20_000);
      const state = snapshot(step >= 4 ? 3 : 2);
      if (field === 'phase') state.file[0] = [0, 1, 2, 7, 3][Math.min(step, 4)]!;
      else state.link[{ serial: 18, acked: 8, ending: 15 }[field]] = field === 'ending' ? 6 - step : step;
      return state;
    });
    const done = expect(waitInstrumentReady(read)).resolves.toMatchObject({ file: [3, 0, -35, 2] });
    await vi.advanceTimersByTimeAsync(80_000);
    await done;
    expect(vi.getTimerCount()).toBe(0);
  },
);

test('a memory RPC that never answers cannot bypass the watchdog', async () => {
  const failed = expect(waitInstrumentReady(() => new Promise<number>(() => {}))).rejects.toThrow(
    'Instrument load progress read failed',
  );
  await vi.advanceTimersByTimeAsync(30_000);
  await failed;
  expect(vi.getTimerCount()).toBe(0);
});
