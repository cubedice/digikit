import { button, withinRuntimeDeadline, type Session } from '@digi/test-support';
import { aggregate } from '../support.ts';
import { deviceProfile, portAnchor } from './device-data.ts';

type PanelSession = Pick<Session, 'options' | 'status'> & {
  client: Pick<Session['client'], 'inputButton' | 'inputTurn' | 'memPeek'>;
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

  /** A turn may be filtered without producing a view event (finding 78).
   * Wait for the UART decoder, its pending-count flush, and the UI handler;
   * input_pending alone only acknowledges the UART delivery. The two literals
   * are inside the hashed decoder (finding 58): +0x10 is the encoder map and
   * +0x34 the base of eight-byte {pending delta, timestamp} rows. The pending
   * row uses the mapped one-based encoder, not its zero-based click-key index.
   * This is an input acknowledgement, not a DSP/configuration acknowledgement. */
  async function turn(encoder: number, detents: number): Promise<void> {
    if (!Number.isInteger(encoder) || encoder < 1 || encoder > profile.panel.encoders)
      throw new Error(`No panel encoder ${encoder} on ${device}`);
    if (!Number.isInteger(detents) || !detents || Math.abs(detents) > 30)
      throw new Error('An acknowledged turn requires 1..30 signed detents');
    const deadline = Date.now() + timeoutMs;
    let phase = 'layout',
      last: unknown;
    const word = async (address: number) => {
      const value = await withinRuntimeDeadline(deadline, () => session.client.memPeek(address));
      if (value === null) throw new Error(`Unmapped encoder memory at ${address.toString(16)}`);
      return value >>> 0;
    };
    const until = async <T>(read: () => Promise<T>, done: (value: T) => boolean) => {
      for (;;) {
        last = await withinRuntimeDeadline(deadline, read);
        if (done(last as T)) return;
        if (Date.now() >= deadline) throw new Error('Encoder acknowledgement deadline expired');
        await new Promise<void>((resolve) => setTimeout(resolve, Math.min(pollMs, deadline - Date.now())));
      }
    };
    const status = () => withinRuntimeDeadline(deadline, () => session.status());
    const loop = async () => {
      const hits = (await status()).heartbeat.mainloop_hits;
      if (!Number.isSafeInteger(hits) || hits < 0) throw new Error('Missing UI mainloop counter');
      return hits;
    };
    const nextLoop = async () => {
      const before = await loop();
      await until(loop, (hits) => hits > before);
    };
    try {
      const decoder = portAnchor(device, firmware, 'cf.input.fn.enc_decoder');
      if ((await word(decoder + 0x30)) !== 0x2440d5fc) throw new Error('Unexpected encoder pending-count instruction');
      const mapped = await word((await word(decoder + 0x10)) + 4 * (encoder - 1));
      if (mapped < 1 || mapped > profile.panel.encoders) throw new Error(`Invalid mapped encoder ${mapped}`);
      const pending = (await word(decoder + 0x34)) + 8 * mapped;
      const queue = await word(queueLiteral);
      phase = 'send';
      await withinRuntimeDeadline(deadline, () => session.client.inputTurn(encoder, detents));
      phase = 'UART';
      await until(status, (s) => s.cf.input_pending === 0 && s.scheduled_inputs === 0);
      // The final byte can have left the UART while its decoder still executes.
      await nextLoop();
      phase = 'flush';
      await until(
        () => word(pending),
        (delta) => delta === 0,
      );
      phase = 'queue';
      await until(
        () => word(queue + 4),
        (count) => count === 0,
      );
      phase = 'handler';
      await nextLoop();
    } catch (cause) {
      throw new Error(`Encoder ${encoder} turn failed at ${phase}: ${JSON.stringify(last)}`, { cause });
    }
  }

  return { edge, tap, turn };
}
