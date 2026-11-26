import type { Session } from '@digi/test-support';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { serviceMapEntries, serviceSymbols } from './service-symbols.ts';
import { sessionRt } from './session-rt.ts';
import { SETTLE } from './panel-waits.ts';

/**
 * A name lookup for a test that reads the state of code a card service runs for an image adapter (docs/plans/fork-image-diet.md section 9):
 * a name of one of `services` (the first whose address map has it) resolves through that service's load address, every other name (the
 * DSP symbols, the runtime, the image part of the adapters) through the plan. The services are found in the device's load table: the
 * lookup waits, in emulated time, until the boot has loaded each. Call it after every boot of the session, and again after a restart (a
 * service may load elsewhere).
 */
export async function loadedServiceSymbols(
  session: Session,
  plan: RuntimePlan,
  services: string[],
  attempts = 120,
): Promise<(name: string) => number> {
  const lookups: { entries: Map<string, number>; resolve: (name: string) => number }[] = [];
  for (const service of services) {
    for (let i = 0; ; i++) {
      try {
        const resolve = await serviceSymbols({ session }, plan, service);
        lookups.push({ entries: serviceMapEntries(session.options.device, service), resolve });
        break;
      } catch (e) {
        if (i >= attempts || !String(e).includes('is not in the load table')) throw e;
        await sessionRt(session).ui.wait({ ms: 500, why: SETTLE });
      }
    }
  }
  return (name) => (lookups.find((l) => l.entries.has(name))?.resolve ?? ((n: string) => planSymbol(plan, n)))(name);
}
