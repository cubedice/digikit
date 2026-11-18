import { button, type Session } from '@digi/test-support';
import { afterEach, beforeEach, expect, test, vi } from 'vitest';
import { portAnchorAddress } from '../runtime/device-data.ts';
import { acknowledgedPanel } from '../runtime/panel-ack.ts';

beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(0);
});
afterEach(() => vi.useRealTimers());

function fixture({ device = 'dt2' as const, firmware = '1.17', stalled = false, filteredTurn = false } = {}) {
  const mask = Buffer.alloc(8),
    maskAddress = 0x1000,
    queueAddress = 0x2000;
  const maskLiteral = portAnchorAddress(device, firmware, 'cf.input.site.key_decoder.prev_mask_lit');
  const queueLiteral = portAnchorAddress(device, firmware, 'cf.view.site.ui_loop.queue_lit');
  const decoder = portAnchorAddress(device, firmware, 'cf.input.fn.enc_decoder');
  const mapAddress = 0x3000,
    pendingBase = 0x4000;
  const events: { code: number; down: boolean; time: number; mask: string }[] = [];
  let count = 0,
    hits = 5,
    packets = 0,
    pendingDelta = 0;
  const inputButton = vi.fn(async (code: number, down: boolean) => {
    events.push({ code, down, time: Date.now(), mask: mask.toString('hex') });
    if (stalled && down) return;
    count = 1;
    // The socket/ISR acknowledges immediately; native decode and the actual UI
    // handler complete independently and much later.
    setTimeout(() => {
      const [channel, bit] = code <= 48 ? [Math.floor((code - 1) / 8), (code - 1) % 8] : [6, code === 49 ? 1 : 0];
      mask[channel!] = down ? mask[channel!]! | (1 << bit!) : mask[channel!]! & ~(1 << bit!);
    }, 80);
    setTimeout(() => {
      count = 0;
    }, 160);
    setTimeout(() => {
      hits++;
    }, 240);
  });
  const inputTurn = vi.fn(async (_encoder: number, detents: number) => {
    packets = 1;
    setTimeout(() => {
      packets = 0; // final byte consumed, decoder still in flight
    }, 40);
    setTimeout(() => {
      pendingDelta = filteredTurn ? 0 : detents;
    }, 80);
    setTimeout(() => {
      hits++;
    }, 100);
    setTimeout(() => {
      pendingDelta = 0;
      count = filteredTurn ? 0 : 1;
    }, 160);
    setTimeout(() => {
      count = 0;
    }, 200);
    setTimeout(() => {
      hits++;
    }, 240);
  });
  const memPeek = vi.fn(async (address: number): Promise<number | null> => {
    if (address === maskLiteral) return maskAddress;
    if (address === queueLiteral) return queueAddress;
    if (address === queueAddress + 4) return count;
    if (address === maskAddress || address === maskAddress + 4) return mask.readUInt32BE(address - maskAddress);
    if (address === decoder + 0x30) return 0x2440d5fc;
    if (address === decoder + 0x10) return mapAddress;
    if (address === decoder + 0x34) return pendingBase;
    if (address >= mapAddress && address < mapAddress + 36) return (address - mapAddress) / 4 + 1;
    if (address >= pendingBase + 8 && address <= pendingBase + 72) return pendingDelta;
    return null;
  });
  const session = {
    options: { device, firmware },
    client: { inputButton, inputTurn, memPeek },
    status: async () => ({ cf: { input_pending: packets }, scheduled_inputs: 0, heartbeat: { mainloop_hits: hits } }),
  } as unknown as Session;
  return {
    session,
    events,
    inputButton,
    inputTurn,
    memPeek,
    mask,
    setHits: (value: number) => {
      hits = value;
    },
  };
}

test('tap waits for decoded down, queue drain, and handler completion before sending up', async () => {
  const f = fixture();
  let finished = false;
  const done = acknowledgedPanel(f.session, { pollMs: 20 })
    .tap('SRC')
    .then(() => {
      finished = true;
    });
  await vi.advanceTimersByTimeAsync(60);
  expect(f.events.map((e) => e.down)).toEqual([true]);
  await vi.advanceTimersByTimeAsync(60); // decoded, still queued
  expect(f.events).toHaveLength(1);
  await vi.advanceTimersByTimeAsync(80); // popped, handler still running
  expect(f.events).toHaveLength(1);
  expect(finished).toBe(false);
  await vi.advanceTimersByTimeAsync(40);
  expect(f.events.map((e) => [e.down, e.time])).toEqual([
    [true, 0],
    [false, 240],
  ]);
  await vi.advanceTimersByTimeAsync(240);
  await done;
  expect(finished).toBe(true);
  expect(f.mask.equals(Buffer.alloc(8))).toBe(true);
  expect(vi.getTimerCount()).toBe(0);
});

test('held modifiers remain decoded through the inner tap and are released afterward', async () => {
  const f = fixture();
  const panel = acknowledgedPanel(f.session, { pollMs: 20 });
  const done = (async () => {
    await panel.edge('FUNC', true);
    try {
      await panel.tap('SRC');
    } finally {
      await panel.edge('FUNC', false);
    }
  })();
  await vi.advanceTimersByTimeAsync(960);
  await done;
  expect(f.events.map(({ code, down }) => [code, down])).toEqual([
    [button('FUNC'), true],
    [button('SRC'), true],
    [button('SRC'), false],
    [button('FUNC'), false],
  ]);
  const code = button('FUNC'),
    channel = Math.floor((code - 1) / 8),
    bit = (code - 1) % 8;
  for (const event of f.events.slice(1)) expect(Buffer.from(event.mask, 'hex')[channel]! & (1 << bit)).not.toBe(0);
  expect(f.mask.equals(Buffer.alloc(8))).toBe(true);
});

test('the exceptional last-channel key is acknowledged using its profile mapping', async () => {
  const f = fixture();
  const done = acknowledgedPanel(f.session, { pollMs: 20 }).edge(49, true);
  await vi.advanceTimersByTimeAsync(240);
  await done;
  expect(f.mask[6]).toBe(2);
});

test('a decoder that never receives the press times out and tap still releases the key', async () => {
  const f = fixture({ stalled: true });
  const done = expect(acknowledgedPanel(f.session, { timeoutMs: 300, pollMs: 20 }).tap('SRC')).rejects.toThrow(
    'Panel SRC down failed at decoder',
  );
  await vi.advanceTimersByTimeAsync(600);
  await done;
  expect(f.events.map((e) => e.down)).toEqual([true, false]);
  expect(vi.getTimerCount()).toBe(0);
});

test('a hung memory RPC cannot evade the edge deadline', async () => {
  const f = fixture();
  f.memPeek.mockImplementation(() => new Promise<number | null>(() => {}));
  const done = expect(acknowledgedPanel(f.session, { timeoutMs: 300 }).edge('SRC', true)).rejects.toThrow(
    'Panel SRC down failed at send',
  );
  await vi.advanceTimersByTimeAsync(300);
  await done;
  expect(vi.getTimerCount()).toBe(0);
});

test('tap retains both the original press failure and a failed release', async () => {
  const f = fixture();
  const pressError = new Error('press transport failed');
  const releaseError = new Error('release transport failed');
  f.inputButton.mockRejectedValueOnce(pressError).mockRejectedValueOnce(releaseError);
  await expect(acknowledgedPanel(f.session).tap('SRC')).rejects.toMatchObject({
    name: 'AggregateError',
    errors: [
      { message: expect.stringContaining('Panel SRC down failed at send'), cause: pressError },
      { message: expect.stringContaining('Panel SRC up failed at send'), cause: releaseError },
    ],
  });
  expect(f.inputButton.mock.calls).toEqual([
    [button('SRC'), true],
    [button('SRC'), false],
  ]);
  expect(vi.getTimerCount()).toBe(0);
});

test('a missing mainloop counter cannot be mistaken for handler completion', async () => {
  const f = fixture();
  f.setHits(Number.NaN);
  const done = expect(acknowledgedPanel(f.session, { pollMs: 20 }).edge('SRC', true)).rejects.toThrow(
    'Panel SRC down failed at handler',
  );
  await vi.advanceTimersByTimeAsync(240);
  await done;
});

test('a key absent from the device profile is rejected without sending input', async () => {
  const f = fixture();
  await expect(acknowledgedPanel(f.session).edge(54, true)).rejects.toThrow('No panel key 54 on dt2');
  expect(f.inputButton).not.toHaveBeenCalled();
});

test('turn waits beyond UART delivery for the decoder flush, queue, and handler', async () => {
  const f = fixture();
  let finished = false;
  const done = acknowledgedPanel(f.session, { pollMs: 20 })
    .turn(2, 1)
    .then(() => {
      finished = true;
    });
  await vi.advanceTimersByTimeAsync(60);
  expect(finished).toBe(false); // UART empty but pending count not published yet
  await vi.advanceTimersByTimeAsync(60);
  expect(finished).toBe(false); // decoder finished, delta still pending
  await vi.advanceTimersByTimeAsync(60);
  expect(finished).toBe(false); // flush finished, UI event still queued
  await vi.advanceTimersByTimeAsync(40);
  expect(finished).toBe(false); // queue empty, handler still in flight
  await vi.advanceTimersByTimeAsync(20);
  await done;
  expect(finished).toBe(true);
  expect(f.inputTurn.mock.calls).toEqual([[2, 1]]);
  expect(f.memPeek).toHaveBeenCalledWith(0x4010); // one-based mapped encoder row
  expect(vi.getTimerCount()).toBe(0);
});

test('a filtered encoder packet completes without requiring a view event', async () => {
  const f = fixture({ filteredTurn: true });
  const done = acknowledgedPanel(f.session, { pollMs: 20 }).turn(2, -1);
  await vi.advanceTimersByTimeAsync(240);
  await done;
  expect(f.inputTurn.mock.calls).toEqual([[2, -1]]);
});

test('a pending encoder delta cannot be mistaken for completed input', async () => {
  const f = fixture();
  const original = f.memPeek.getMockImplementation()!;
  f.memPeek.mockImplementation(async (address) => (address === 0x4010 ? 1 : original(address)));
  const done = expect(acknowledgedPanel(f.session, { timeoutMs: 300, pollMs: 20 }).turn(2, 1)).rejects.toThrow(
    'Encoder 2 turn failed at flush',
  );
  await vi.advanceTimersByTimeAsync(300);
  await done;
  expect(f.inputTurn).toHaveBeenCalledTimes(1);
  expect(vi.getTimerCount()).toBe(0);
});

test('an unexpected decoder layout fails before sending input', async () => {
  const f = fixture();
  f.memPeek.mockResolvedValue(0);
  await expect(acknowledgedPanel(f.session).turn(2, 1)).rejects.toThrow('failed at layout');
  expect(f.inputTurn).not.toHaveBeenCalled();
});
