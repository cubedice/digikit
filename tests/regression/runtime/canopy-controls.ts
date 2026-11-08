import { withinRuntimeDeadline } from '@digi/test-support';

/** Read a fully applied CPS9 custom control. Native input must be acknowledged
 * separately: matching revisions alone cannot prove an encoder was consumed.
 * CsState holds 28 big-endian uint16 controls after seven uint32 fields. CPS9
 * stores legacy controls in low halves of words16..31 and WT controls in high
 * halves of words16..27. TUNE/LEVEL are native controls and excluded here. */
export function canopyControls(
  cf: (name: string, index?: number) => Promise<number>,
  rawBank: () => Promise<number[]>,
  { timeoutMs = 30_000, pollMs = 50 } = {},
) {
  async function settled(control: number): Promise<number> {
    if (!Number.isInteger(control) || control < 0 || control >= 28)
      throw new Error(`Invalid CANOPY custom control ${control}`);
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const [phase, requested, applied, pending] = await withinRuntimeDeadline(deadline, () =>
        Promise.all([cf('ip_control'), cf('src_state', 3), cf('ip_control', 6), cf('src_state', 4)]),
      );
      if (phase === 3 && requested === applied && pending === 0) break;
      if (Date.now() >= deadline) throw new Error(`CANOPY control ${control} configuration did not settle`);
      await new Promise<void>((resolve) => setTimeout(resolve, Math.min(pollMs, deadline - Date.now())));
    }
    const [bank, pair] = await withinRuntimeDeadline(deadline, () =>
      Promise.all([rawBank(), cf('src_state', 7 + Math.floor(control / 2))]),
    );
    if (bank.length !== 32) throw new Error(`Invalid CPS9 bank length ${bank.length}`);
    const value = control < 16 ? bank[16 + control]! & 65535 : bank[control]! >>> 16;
    const requested = control & 1 ? pair & 65535 : pair >>> 16;
    if (requested !== value)
      throw new Error(`CANOPY control ${control}: settled request ${requested} differs from DSP ${value}`);
    return value;
  }
  return { settled };
}
