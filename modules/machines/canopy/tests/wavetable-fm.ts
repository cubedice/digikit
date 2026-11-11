import { expect } from 'vitest';

/** The compiled native SRC flow supplies its existing acknowledged panel and
 * audio helpers. Keep the machine-specific behaviour with CANOPY. */
export async function proveWavetableFm(ui: {
  page: (page: number) => Promise<void>;
  turn: (encoder: number, delta: number) => Promise<unknown>;
  set: (encoder: number, control: number, value: number) => Promise<unknown>;
  read: (control: number) => Promise<number>;
  bank: () => Promise<number[]>;
  ownership: () => Promise<unknown>;
  audio: (name: string) => Promise<number>;
  capture: (name: string) => Promise<unknown>;
  values: (name: string) => Promise<unknown>;
  sourceLevel: (direction: 1 | -1) => Promise<void>;
}) {
  const owner = await ui.ownership();
  await ui.page(3);
  expect((await ui.bank()).slice(28).map((v) => v >>> 16)).toEqual([0, 3, 100, 100]);
  // RAT is a discrete list with the same four-tick movement as WAVE/SHAP.
  await ui.turn(2, 1);
  expect(await ui.read(29)).toBe(3);
  await ui.turn(2, 3);
  expect(await ui.read(29)).toBe(4);
  for (const [encoder, control, value] of [
    [1, 28, 35],
    [2, 29, 8],
    [3, 30, 125],
  ] as const) {
    await ui.set(encoder, control, value);
    expect(await ui.read(control)).toBe(value);
  }
  expect(await ui.ownership()).toEqual(owner);
  await ui.capture('canopy-wavetable-fm');
  await ui.values('canopy-wavetable-fm-values');
  await ui.page(2);
  await ui.set(6, 24, 10);
  await ui.set(7, 25, 25);
  const beforeLevel = await ui.bank();
  const full = await ui.audio('wavetable-fm-full');
  expect(full).toBeGreaterThan(1e-5);
  await ui.sourceLevel(-1);
  const silent = await ui.audio('wavetable-fm-source-zero');
  expect(silent).toBeLessThan(full * 0.01);
  expect(await ui.bank()).toEqual(beforeLevel);
  await ui.sourceLevel(1);
  await ui.page(2);
  const position = await ui.read(17);
  const scan = await ui.read(18);
  await ui.set(1, 17, position + 25);
  await ui.set(2, 18, scan + 10);
  expect(await ui.ownership()).toEqual(owner);
  const moved = await ui.audio('wavetable-fm-scan');
  expect(moved).toBeGreaterThan(1e-5);
  return { full, silent, moved, controls: (await ui.bank()).slice(28).map((v) => v >>> 16) };
}
