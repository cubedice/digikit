import fs from 'node:fs';
import { encoder, type Session } from '@digi/test-support';
import { expect } from 'vitest';
import { REDRAW, SETTLE, bounded, word } from '@digi/regression/runtime/fx-live.ts';
import { mainPage as mainPageOf, openPages as openPagesOf } from '@digi/regression/runtime/fx-pages-ui.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';

/** The layout of the phrase control and status words and the raw values (PH_*), read from the one file the provider, the page and the engine share. */
export const PH: Record<string, number> = Object.fromEntries(
  [
    ...fs
      .readFileSync(new URL('../../../../sdk/dsp/contract/dk_phrase.inc', import.meta.url), 'utf8')
      .matchAll(/^#define (PH_\w+) (0x[0-9a-fA-F]+|\d+)$/gm),
  ].map((match) => [match[1]!, Number(match[2])]),
);

/** The float a control or status word holds. */
export const float = (bits: number) => {
  const bytes = Buffer.alloc(4);
  bytes.writeUInt32LE(bits >>> 0);
  return bytes.readFloatLE();
};

/** Press and release a key (with `held` down around it), then let the firmware redraw for `settleMs` of emulated time. */
export function tap(session: Session, key: string, settleMs = 300, held?: string): Promise<void> {
  return sessionRt(session).ui.tap(key, { until: { ms: Math.max(settleMs, 1), why: REDRAW }, ...(held ? { held } : {}) });
}

/** Let `ms` of emulated time pass. */
export function advance(session: Session, ms: number): Promise<void> {
  return sessionRt(session).ui.wait({ ms, why: SETTLE });
}

export const mainPage = (session: Session) => mainPageOf(sessionRt(session));
export const openPages = (session: Session) => openPagesOf(sessionRt(session));

/** Show PRISM's page in the FX view (`phrase_ui_debug`: word 6 is "the page is on top", word 7 the view's page). */
export async function openPrismPage(session: Session, debugAddress: number): Promise<void> {
  await mainPage(session);
  await openPages(session);
  // Three stock effects and at most four digikit effects; FUNC + FX goes to the next one.
  for (let attempt = 0; attempt < 8; attempt++) {
    if (await word(session, debugAddress + 24)) return;
    await tap(session, 'FX', 300, 'FUNC');
  }
  throw new Error(`PRISM page unavailable; last native page=${await word(session, debugAddress + 28)}`);
}

export interface PrismCell {
  label: string;
  value: string;
  state: string;
  lit: boolean;
  /** 0: no crossfader drawn; else 1 + its position as it sounds (0 .. 127) */
  fader: number;
}
export interface PrismPage {
  title: string;
  zone: string;
  time: string;
  /** the header's loop line while PRISM hears the loop ('' otherwise) */
  line: string;
  mix: number;
  zoneIndex: number;
  cells: PrismCell[];
}

/** The page as core/phrase-ui last painted it (`phrase_ui_page`, the `PhrasePage` of its render.h): the texts of the header and of the cells A to H. */
export async function prismPage(session: Session, address: number): Promise<PrismPage> {
  // title, zone, time (8 bytes each), line (24), mix, zone index, then 8 cells of label, value, state (8 each), lit, fader
  const bytes = Buffer.alloc(312);
  for (let i = 0; i < 78; i++) bytes.writeUInt32BE(await word(session, address + 4 * i), 4 * i);
  const text = (at: number, field = 8) => {
    const end = bytes.indexOf(0, at);
    return bytes.toString('latin1', at, Math.min(end < 0 ? at + field : end, at + field));
  };
  return {
    title: text(0),
    zone: text(8),
    time: text(16),
    line: text(24, 24),
    mix: bytes.readUInt32BE(48),
    zoneIndex: bytes.readUInt32BE(52),
    cells: Array.from({ length: 8 }, (_, i) => ({
      label: text(56 + 32 * i),
      value: text(64 + 32 * i),
      state: text(72 + 32 * i),
      lit: bytes.readUInt32BE(80 + 32 * i) !== 0,
      fader: bytes.readUInt32BE(84 + 32 * i),
    })),
  };
}

/**
 * Turn `knob` until the raw value `index` (`phrase_ui_values`, a PH_P_* index) is `target`. `detent`: the value is a choice that takes four
 * ticks (ZONE, and RATE's encoder with SYNC); `coarse`: one tick moves the value by this much (RATE free: PH_RATE_COARSE).
 */
export async function setPrism(
  session: Session,
  values: number,
  index: number,
  knob: string,
  target: number,
  step: { detent?: boolean; coarse?: number } = {},
): Promise<void> {
  const value = () => word(session, values + 4 * index);
  for (let attempt = 0; attempt < 60; attempt++) {
    const current = await value();
    if (current === target) return;
    const error = target - current;
    const ticks = step.detent
      ? Math.sign(error) * 4
      : Math.max(-8, Math.min(8, Math.trunc(error / (step.coarse ?? 1)) || Math.sign(error)));
    await bounded(() => session.client.inputTurn(encoder(knob), ticks));
    await advance(session, 40);
  }
  expect(await value(), `${knob} reaches ${target}`).toBe(target);
}
