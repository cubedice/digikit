import type { Session } from '@digi/test-support';
import { REDRAW, SETTLE, word } from '@digi/regression/runtime/fx-live.ts';
import { mainPage as mainPageOf, openPages as openPagesOf } from '@digi/regression/runtime/fx-pages-ui.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';

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

/** Select the one global PRISM entry without changing its recalled local page. */
export async function openPrismPage(session: Session, debugAddress: number): Promise<void> {
  // NO first closes IN if it owns the gesture; a second NO then exits FX.
  if (await word(session, debugAddress + 8)) await mainPage(session);
  await mainPage(session);
  await openPages(session);
  // Three stock effects and at most four dynamic slots. Do not use DOWN:
  // the remembered global selection may already be PRISM when FX opens.
  for (let attempt = 0; attempt < 8; attempt++) {
    if (await word(session, debugAddress + 24)) return;
    await tap(session, 'FX', 300, 'FUNC');
  }
  throw new Error(`PRISM page unavailable; last native page=${await word(session, debugAddress + 28)}`);
}
