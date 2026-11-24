import type { Session } from '@digi/test-support';
import type { RuntimePlan } from './runtime-plan.ts';
import { loadedServiceSymbols } from './loaded-service-symbols.ts';

/**
 * A name lookup for a test of CANOPY or RELAY. The host code of core/instrument-play and core/midi-events and all its state run from
 * services/instrument.dks (docs/plans/fork-image-diet.md section 9), so a name of the service (`ip_control`, `ip_tracks`, `src_state`, ...)
 * resolves through its load address, and every other name (the DSP symbols, the runtime, the image part of the adapters such as
 * `ip_sa_info`) through the plan. The service is found in the device's load table: the lookup waits, in emulated time, until the boot has
 * loaded it (`loadedServiceSymbols`). Call it after every boot of the session, and again after a restart (the service may load elsewhere).
 */
export function instrumentSymbols(
  session: Session,
  plan: RuntimePlan,
  attempts = 120,
): Promise<(name: string) => number> {
  return loadedServiceSymbols(session, plan, ['instrument'], attempts);
}
