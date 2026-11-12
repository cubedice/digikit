import { withinRuntimeDeadline } from '@digi/test-support';

/** Reach an exact integer control value with one acknowledged packet at a time.
 * maxStep opts linear controls into larger packets; the default keeps single
 * detents. maxTurns limits packets, including consumed-but-filtered packets.
 * readSettled must wait for the requested configuration to be fully applied;
 * turn must acknowledge decoder flush and UI handling, including filtered
 * packets. Neither an unchanged audio bank nor elapsed wall time proves that a
 * queued turn was dropped. A delayed configuration therefore never causes an
 * extra packet. Unexpected acceleration or ownership changes fail explicitly. */
export async function encoderValue(
  turn: (detents: number) => Promise<void>,
  readSettled: () => Promise<number>,
  target: number,
  { maxTurns = 64, maxStep = 1, timeoutMs = 180_000 } = {},
): Promise<void> {
  if (
    !Number.isSafeInteger(target) ||
    !Number.isSafeInteger(maxTurns) ||
    maxTurns < 1 ||
    !Number.isSafeInteger(maxStep) ||
    maxStep < 1
  )
    throw new Error('Encoder target must be a safe integer; turn and step limits must be positive safe integers');
  const deadline = Date.now() + timeoutMs;
  let current = await withinRuntimeDeadline(deadline, readSettled);
  const trace = [current];
  for (let n = 0; n < maxTurns && current !== target; n++) {
    if (!Number.isSafeInteger(current)) throw new Error(`Invalid encoder value ${current}`);
    const step = Math.sign(target - current) * Math.min(Math.abs(target - current), maxStep);
    await withinRuntimeDeadline(deadline, () => turn(step));
    const after = await withinRuntimeDeadline(deadline, readSettled);
    trace.push(after);
    if (after !== current && after !== current + step)
      throw new Error(`Encoder moved unexpectedly toward ${target}: ${trace.join(', ')}`);
    current = after;
  }
  if (current !== target) throw new Error(`Encoder did not reach ${target} in ${maxTurns} turns: ${trace.join(', ')}`);
}
