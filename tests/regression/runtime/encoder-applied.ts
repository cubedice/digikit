import { withinRuntimeDeadline, type Session } from '@digi/test-support';

/** Set a custom integer control through real panel turns, one applied step at a
 * time. The native decoder clamps each periodic encoder flush to +/-30 (finding
 * 58); sending a larger delta, or accumulating several before a flush, loses
 * detents. This is for controls whose detents map directly to integer values,
 * not native accelerated/fractional controls such as TUNE and LEVEL. */
export async function turnAppliedControl(
  session: { client: Pick<Session['client'], 'inputTurn'> },
  encoder: number,
  readApplied: () => Promise<number>,
  target: number,
  { timeoutMs = 15_000, pollMs = 50 } = {},
): Promise<void> {
  if (!Number.isInteger(encoder) || encoder < 1 || encoder > 9) throw new Error(`Invalid panel encoder ${encoder}`);
  if (!Number.isInteger(target) || target < 0 || target > 65535)
    throw new Error(`Invalid applied control target ${target}`);
  const read = async (deadline: number) => {
    const value = await withinRuntimeDeadline(deadline, readApplied);
    if (!Number.isInteger(value) || value < 0 || value > 65535)
      throw new Error(`Encoder ${encoder}: invalid applied value ${value}`);
    return value;
  };
  let current = await read(Date.now() + timeoutMs);
  while (current !== target) {
    const delta = Math.max(-30, Math.min(30, target - current));
    const next = current + delta;
    const deadline = Date.now() + timeoutMs;
    try {
      await withinRuntimeDeadline(deadline, () => session.client.inputTurn(encoder, delta));
      for (;;) {
        const value = await read(deadline);
        if (value === next) break;
        if (value !== current) throw new Error(`Unexpected applied value ${value}`);
        if (Date.now() >= deadline) throw new Error(`Applied value stalled at ${value}`);
        await new Promise<void>((resolve) => setTimeout(resolve, Math.min(pollMs, deadline - Date.now())));
      }
    } catch (cause) {
      throw new Error(`Encoder ${encoder}: failed to apply ${current} -> ${next} toward ${target}`, { cause });
    }
    current = next;
  }
}
