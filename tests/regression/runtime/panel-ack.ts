import { button, withinRuntimeDeadline, type Session } from '@digi/test-support';
import { aggregate } from '../support.ts';
import { deviceProfile, portAnchor } from './device-data.ts';

type PanelSession = Pick<Session, 'options' | 'status'> & {
  client: Pick<Session['client'], 'inputButton' | 'memPeek'>;
};

/** Panel edges acknowledged by the guest, for slow free-running device tests.
 * The native key decoder queues changed-bit events before publishing its previous
 * mask (DT2 1.17: queue-send 0x4011e4a2, mask store 0x4011e502; finding 58).
 * An empty queue can still have a handler in flight, so wait for the next UI
 * loop too. Daemon input_pending only acknowledges delivery to the UART ISR.
 * Callers still poll their semantic result when DSP work or redraw is deferred. */
export function acknowledgedPanel(session: PanelSession, { timeoutMs = 30_000, pollMs = 50 } = {}) {
  const { device, firmware } = session.options;
  const profile = deviceProfile(device);
  if (profile.input.backend !== 'panel-uart') throw new Error(`Unsupported panel backend for ${device}`);
  const maskLiteral = portAnchor(device, firmware, 'cf.input.site.key_decoder.prev_mask_lit');
  const queueLiteral = portAnchor(device, firmware, 'cf.view.site.ui_loop.queue_lit');

  async function edge(name: string | number, down: boolean): Promise<void> {
    const code = button(name);
    const position =
      code >= 1 && code <= profile.panel.linear_channels * 8
        ? [Math.floor((code - 1) / 8), (code - 1) % 8]
        : profile.panel.exceptions[String(code)];
    if (!position || position[0]! >= profile.panel.button_channels)
      throw new Error(`No panel key ${code} on ${device}`);
    const [channel, bit] = position as [number, number];
    const deadline = Date.now() + timeoutMs;
    let phase = 'send',
      last: unknown;
    const word = async (address: number) => {
      const value = await withinRuntimeDeadline(deadline, () => session.client.memPeek(address));
      if (value === null) throw new Error(`Unmapped panel memory at ${address.toString(16)}`);
      return value >>> 0;
    };
    const until = async <T>(read: () => Promise<T>, done: (value: T) => boolean) => {
      for (;;) {
        last = await withinRuntimeDeadline(deadline, read);
        if (done(last as T)) return;
        if (Date.now() >= deadline) throw new Error('Panel acknowledgement deadline expired');
        await new Promise<void>((resolve) => setTimeout(resolve, Math.min(pollMs, deadline - Date.now())));
      }
    };
    const loop = async () => {
      const value = (await withinRuntimeDeadline(deadline, () => session.status())).heartbeat.mainloop_hits;
      if (!Number.isSafeInteger(value) || value < 0) throw new Error('Missing UI mainloop counter');
      return value;
    };
    try {
      await withinRuntimeDeadline(deadline, () => session.client.inputButton(code, down));
      const mask = (await word(maskLiteral)) + channel;
      const queue = await word(queueLiteral);
      phase = 'decoder';
      await until(
        async () => ((await word(mask & ~3)) >>> (8 * (3 - (mask & 3)))) & (1 << bit),
        (value) => Boolean(value) === down,
      );
      phase = 'queue';
      await until(
        () => word(queue + 4),
        (count) => count === 0,
      );
      phase = 'handler';
      const before = await loop();
      await until(loop, (value) => value > before);
    } catch (cause) {
      throw new Error(`Panel ${name} ${down ? 'down' : 'up'} failed at ${phase}: ${JSON.stringify(last)}`, { cause });
    }
  }

  async function tap(name: string | number): Promise<void> {
    let pressFailed = false;
    let pressError: unknown;
    try {
      await edge(name, true);
    } catch (error) {
      pressFailed = true;
      pressError = error;
    }
    try {
      await edge(name, false);
    } catch (error) {
      if (pressFailed) throw aggregate([pressError, error], `Panel ${name} press and release failed`);
      throw error;
    }
    if (pressFailed) throw pressError;
  }

  return { edge, tap };
}
