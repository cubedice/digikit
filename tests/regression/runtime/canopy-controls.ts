import { withinRuntimeDeadline } from '@digi/test-support';

/** Read a fully applied CPS9/CPSA/CPSB custom control. Native input must be acknowledged
 * separately: matching revisions alone cannot prove an encoder was consumed.
 * CsState holds 32 big-endian uint16 controls after seven uint32 fields. CPS9
 * stores legacy controls in low halves of words16..31 and WT controls in high
 * halves of words16..27. CPSA uses those high halves for the active WT/FM family.
 * CPSB adds wavetable FM in the upper halves of words28..31.
 * TUNE/LEVEL are native controls and excluded here. */
export function canopyControls(
  cf: (name: string, index?: number) => Promise<number>,
  rawBank: () => Promise<number[]>,
  { timeoutMs = 30_000, pollMs = 50 } = {},
) {
  async function settled(control: number): Promise<number> {
    if (!Number.isInteger(control) || control < 0 || control >= 32)
      throw new Error(`Invalid CANOPY custom control ${control}`);
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const [phase, requested, applied, pending, tunePending] = await withinRuntimeDeadline(deadline, () =>
        Promise.all([
          cf('ip_control'),
          cf('src_state', 3),
          cf('ip_control', 6),
          cf('src_state', 4),
          cf('src_state', 30),
        ]),
      );
      if (phase === 3 && requested === applied && pending === 0 && tunePending === 0) break;
      if (Date.now() >= deadline) throw new Error(`CANOPY control ${control} configuration did not settle`);
      await new Promise<void>((resolve) => setTimeout(resolve, Math.min(pollMs, deadline - Date.now())));
    }
    const [bank, pair] = await withinRuntimeDeadline(deadline, () =>
      Promise.all([rawBank(), cf('src_state', 7 + Math.floor(control / 2))]),
    );
    if (bank.length !== 32) throw new Error(`Invalid CANOPY configuration bank length ${bank.length}`);
    const value = control < 16 ? bank[16 + control]! & 65535 : bank[control]! >>> 16;
    const requested = control & 1 ? pair & 65535 : pair >>> 16;
    if (requested !== value)
      throw new Error(`CANOPY control ${control}: settled request ${requested} differs from DSP ${value}`);
    return value;
  }
  return { settled };
}
