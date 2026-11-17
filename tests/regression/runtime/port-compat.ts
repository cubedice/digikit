import { emulatorClock, panelInput, waitClock, type Session } from '@digi/test-support';
import type { TestRuntime } from '../support.ts';
import { timePhase } from '../timing.ts';
import { Ui } from './ui.ts';

/**
 * The fixed waits and panel steps that CANOPY's and Overloop's device tests were written against, on the present wait API, so those
 * tests keep their timing. A new test uses `rt.ui`, `rt.listen` and `pollUntil` (docs/plans/test-architecture.md, T2 step 3); a test
 * that uses this file moves to them when it is next changed.
 */

/** DSP frames per emulated millisecond (1500 a second). */
const FRAMES_PER_MS = 1.5;

/** Wait for `ms` of emulated time. */
export function waitEmulated(session: Session, ms: number): Promise<void> {
  return waitClock(emulatorClock(session), ms);
}

/** Wait until `frames` more DSP frames of emulated time have passed. */
export function waitEmulatedFrames(session: Session, frames: number): Promise<void> {
  return waitEmulated(session, frames / FRAMES_PER_MS);
}

/** Press, hold, release (also when the press fails), then let the UI settle. */
export async function tap(
  session: Session,
  send: (down: boolean) => Promise<unknown>,
  holdMs: number,
  settleMs = 1200,
): Promise<void> {
  try {
    await send(true);
    await waitEmulated(session, holdMs);
  } finally {
    await send(false);
  }
  await timePhase('settle.input', () => waitEmulated(session, settleMs));
}

type Tap = (name: string, settleMs?: number) => Promise<void>;

/** SETTINGS > DIGIKIT from any state, with the caller's own tap (`flows/menu.ts` has the `rt` form). */
export async function openDigikitMenu(tap: Tap, checkpoint?: (name: string) => Promise<void>): Promise<void> {
  await tap('NO', 2000);
  await tap('SETTINGS', 3000);
  for (let i = 0; i < 8; i++) await tap('DOWN');
  await checkpoint?.('settings-ninth-row');
  await tap('YES', 3000);
  await checkpoint?.('digikit-main');
}

/** The wait API over a session a test started itself (`withSession`), for the flows that take `rt`. Its waits are not recorded. */
export function sessionRt(session: Session): Pick<TestRuntime, 'session' | 'ui' | 'clock'> {
  const clock = emulatorClock(session);
  return {
    session,
    clock,
    ui: new Ui(
      panelInput(session, clock),
      clock,
      () => undefined,
      () => {},
      session,
    ),
  };
}

/** Rejects after `ms` of wall time with `message`: a guard against a call that hangs, never the length of a wait. */
export function wallGuard(ms: number, message: string): Promise<never> {
  return new Promise<never>((_, reject) => setTimeout(() => reject(new Error(message)), ms));
}
