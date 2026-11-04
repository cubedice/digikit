import fs from 'node:fs';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { button, repoRoot } from '@digi/test-support';
import { expect, onTestFinished, test } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions, settleUi } from './live.ts';
import { captureRemoteOled } from './remote.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';

const h = withSession({ ...liveOptions('dt2'), dsp: 'sharc', restart: false });
test('committed master grid follows internal and paced external clock', async () => {
  const root = repoRoot();
  const image = path.join(root, process.env.CANOPY_CLOCK_IMAGE ?? 'out/patch/canopy-stage4-final.syx');
  const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  const { session } = h;
  const midi = process.env.CANOPY_CLOCK_PORT === 'din' ? 'midi.in' : 'midi.usb.in';
  onTestFinished(async () => {
    const fault = await session.client.call<{ fault_registers?: { a: number[] } }>('diagnostics');
    const sp = fault.fault_registers?.a[7];
    const stack =
      sp === undefined
        ? []
        : await Promise.all(Array.from({ length: 32 }, (_, i) => session.client.memPeek(sp + 4 * i)));
    fs.writeFileSync(
      path.join(root, 'out/canopy-stage4-clock-fault-registers.json'),
      JSON.stringify({ fault, stack }, null, 2),
    );
    fs.writeFileSync(
      path.join(root, 'out/canopy-stage4-clock-health.json'),
      JSON.stringify(await session.status(), null, 2),
    );
  });
  await session.client.firmwareLoad({
    syx: image,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
    card: session.cardPath,
  });
  await session.client.call('run.mode', { mode: 'free' });
  await settleUi(session);
  const key = async (name: string) => {
    await session.client.inputButton(button(name), true);
    await delay(200);
    await session.client.inputButton(button(name), false);
    await delay(500);
  };
  const position = async () => {
    const data = await session.client.call<{ hex: string }>('dsp.peek', {
      addr: planSymbol(plan, '_pattern_position_info'),
      len: 80,
    });
    const b = Buffer.from(data.hex, 'hex');
    let clock: number[] | undefined;
    {
      const raw = await session.client.call<{ hex: string }>('dsp.peek', {
        addr: planSymbol(plan, '_pattern_clock_info'),
        len: 32,
      });
      const c = Buffer.from(raw.hex, 'hex');
      clock = Array.from({ length: 8 }, (_, i) => c.readUInt32LE(i * 4));
    }
    return {
      sequence: b.readUInt32LE(12),
      flags: b.readUInt32LE(20),
      block: b.readUInt32LE(40),
      received: b.readUInt32LE(64),
      deadline: b.readUInt32LE(44),
      step: b.readUInt32LE(28),
      length: b.readUInt32LE(32),
      clock,
    };
  };
  const intervals = async (count: number, label: string) => {
    const result: number[] = [];
    const observations: unknown[] = [];
    let previous = await position();
    await expect
      .poll(
        async () => {
          const health = await session.status();
          expect(health.cf?.error, JSON.stringify(health)).toBeNull();
          const now = await position();
          if (now.sequence !== previous.sequence) {
            observations.push(now);
            fs.writeFileSync(
              path.join(root, `out/canopy-stage4-clock-${label}.json`),
              JSON.stringify({ observations, result }, null, 2),
            );
            if (now.sequence === previous.sequence + 1 && now.flags === 3 && previous.flags === 3)
              result.push((now.block - previous.block) >>> 0);
            previous = now;
          }
          return result.length;
        },
        { timeout: 180000, interval: 100 },
      )
      .toBeGreaterThanOrEqual(count);
    return result;
  };
  await key('PLAY');
  const internal = await intervals(4, 'internal');
  console.log({ internal });
  fs.writeFileSync(path.join(root, 'out/canopy-stage4-clock.json'), JSON.stringify({ internal }, null, 2));
  await key('TEMPO');
  await captureRemoteOled(session, 'canopy-clock-tempo');
  await key('NO');
  expect(internal.every((n) => n >= 220 && n <= 221)).toBe(true);
  await key('STOP');
  // Exercise stock scheduling at twice the master scale and a seven-step
  // wrap. These are pattern fixture inputs, never fabricated provider output.
  const pattern = (await session.client.memPeek(0x4316501c))!;
  const scaleWord = (await session.client.memPeek(pattern + 0x1db78))!;
  const lengthWord = (await session.client.memPeek(pattern + 0x1db74))!;
  const wrapWord = (await session.client.memPeek(0x43165130))!;
  await session.client.call('mem.cf.write', { addr: pattern + 0x1db7a, hex: '00' });
  await session.client.call('mem.cf.write', { addr: pattern + 0x1db74, hex: '0007' });
  await session.client.call('mem.cf.write', { addr: 0x43165130, hex: '0007' });
  await key('PLAY');
  await session.client.call('mem.cf.write', { addr: 0x43165130, hex: '0007' });
  const scaled = await intervals(9, 'scaled');
  expect(scaled.slice(-4).every((n) => n >= 110 && n <= 111)).toBe(true);
  expect((await position()).length).toBe(7);
  await key('STOP');
  await session.client.call('mem.cf.write', {
    addr: pattern + 0x1db7a,
    hex: ((scaleWord >>> 8) & 255).toString(16).padStart(2, '0'),
  });
  await session.client.call('mem.cf.write', { addr: 0x43165130, hex: (wrapWord >>> 16).toString(16).padStart(4, '0') });
  await session.client.call('mem.cf.write', {
    addr: pattern + 0x1db74,
    hex: (lengthWord >>> 16).toString(16).padStart(4, '0'),
  });
  for (const [addr, value] of [
    [0x429368ca, 3],
    [0x42936872, 0],
    [0x429368d6, 1], // explicitly enable stock MIDI clock receive
  ]) {
    const b = Buffer.alloc(4);
    b.writeUInt32BE(value!);
    await session.client.call('mem.cf.write', { addr, hex: b.toString('hex') });
  }
  // USB parser receives real MIDI realtime bytes at the emulator's 132 MHz
  // instruction clock. No position or timing-provider memory is fabricated.
  await session.client.call(midi, { bytes: [0xfa, ...Array(96).fill(0xf8)], chunk: 1, period: 1833333 });
  const external = await intervals(8, 'external');
  console.log({ external });
  fs.writeFileSync(path.join(root, 'out/canopy-stage4-clock.json'), JSON.stringify({ internal, external }, null, 2));
  expect(external.slice(-4).every((n) => n >= 123 && n <= 127)).toBe(true);
  // Stock transport may freewheel; only the separate instrument clock expires.
  const lost = await position();
  await expect
    .poll(
      async () => {
        const now = await position();
        return now.clock?.[4];
      },
      { timeout: 180000, interval: 200 },
    )
    .toBe(0);
  const stillLost = await position();
  fs.writeFileSync(path.join(root, 'out/canopy-stage4-clock-loss.json'), JSON.stringify({ lost, stillLost }, null, 2));
  expect(stillLost.flags).toBe(3); // legacy position semantics are unchanged
  await session.client.call(midi, { bytes: [...Array(96).fill(0xf8), 0xfc], chunk: 1, period: 2750000 });
  const recovered = await intervals(8, 'recovered');
  expect(recovered.slice(-4).every((n) => n >= 185 && n <= 190)).toBe(true);
  await expect.poll(async () => (await position()).flags, { timeout: 120000 }).toBe(0);
}, 900000);
