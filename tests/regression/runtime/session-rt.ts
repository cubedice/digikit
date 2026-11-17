import { emulatorClock, panelInput, type Session } from '@digi/test-support';
import type { TestRuntime } from '../support.ts';
import { Ui } from './ui.ts';

/**
 * The wait API (`rt.ui`, the clock) over a session a test started itself with `withSession`, for its own waits and for the flows
 * that take `rt` (`flows/menu.ts`, `project-kit.ts`). A test of `runtimeTests` has the full `TestRuntime` and does not need this.
 * The waits are not recorded as timing phases, and there are no runtime events (`rt.listen`): wait on state or with a stated reason.
 */
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
