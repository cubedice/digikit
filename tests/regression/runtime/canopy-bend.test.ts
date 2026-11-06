import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { button, repoRoot } from '@digi/test-support';
import { expect, test } from 'vitest';
import { withSession } from '../support.ts';
import { settleUi } from './waits.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';

const h = withSession({
  device: 'dt2',
  firmware: '1.17',
  dsp: 'sharc',
  restart: false,
  binary: process.env.DIGI_EMUD_DT2,
});

test('route pitch bend and controller reset reach final audio at the requested pitch', async () => {
  const root = repoRoot();
  const image = path.join(root, 'out/patch/canopy-live-carrier.syx');
  const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  expect(plan.modules).toContain('test/canopy-carrier@0.1.0');
  expect(plan.base.syx_sha256).toBe('26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5');
  const sha = createHash('sha256').update(fs.readFileSync(image)).digest('hex');
  expect(plan.output?.syx_sha256).toBe(sha);
  const { session } = h;
  await session.client.firmwareLoad({
    syx: image,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
  });
  await session.client.call('run.mode', { mode: 'free' });
  const status = planSymbol(plan, 'cp_transport_proof');
  const word = (i: number) => session.client.memPeek(status + 4 * i);
  const readVoices = async () => {
    const r = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', {
      addr: planSymbol(plan, '_cp_banks'),
      len: 256,
    });
    expect(r.unknown).toBe(0);
    const b = Buffer.from(r.hex, 'hex');
    return Array.from({ length: 4 }, (_, i) => ({ token: b.readUInt32LE(i * 32), gate: b.readUInt32LE(i * 32 + 4) }))
      .filter((v) => v.token !== 0)
      .sort((a, b) => a.token - b.token);
  };
  const key = async (name: string) => {
    await session.client.inputButton(button(name), true);
    await delay(300);
    await session.client.inputButton(button(name), false);
    await delay(1200);
  };
  const modified = async (modifier: string, name: string) => {
    await session.client.inputButton(button(modifier), true);
    try {
      await key(name);
    } finally {
      await session.client.inputButton(button(modifier), false);
    }
  };
  await expect.poll(() => word(0), { timeout: 30_000 }).toBe(1);
  await settleUi(session);
  for (const [addr, value] of [
    [0x429368ca, 3],
    [0x42936872, 0],
  ] as const) {
    const b = Buffer.alloc(4);
    b.writeUInt32BE(value);
    await session.client.call('mem.cf.write', { addr, hex: b.toString('hex') });
  }
  await modified('FUNC', 'SRC');
  await key('DOWN');
  await key('YES');
  await key('YES');
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  const pcm = () =>
    session.client.call<{ samples: number; rms: number; non_finite: number }>('pcm.tap', { action: 'report' });
  const measurePitch = async (name: string, expected: number) => {
    await session.client.call('pcm.tap', { action: 'start' });
    await expect.poll(async () => (await pcm()).samples, { timeout: 20_000 }).toBeGreaterThanOrEqual(24_000);
    const capture = path.join(root, `out/canopy-bend-${name}.f32le`);
    await session.client.call('pcm.tap', { action: 'capture', path: capture });
    try {
      await session.client.call('pcm.tap', { action: 'start' });
      await expect.poll(async () => (await pcm()).samples, { timeout: 20_000 }).toBeGreaterThanOrEqual(24_000);
    } finally {
      await session.client.call('pcm.tap', { action: 'capture_stop' });
    }
    const raw = fs.readFileSync(capture);
    expect(raw.length % 8).toBe(0);
    expect(raw.length).toBeGreaterThanOrEqual(24_000 * 4);
    const crossings: number[] = [];
    for (let offset = 0; offset < raw.length; offset += 4) expect(Number.isFinite(raw.readFloatLE(offset))).toBe(true);
    // Interpolate positive zero crossings on LEFT only (48 kHz stereo f32le).
    // Use first-to-last period spacing, not a rounded count per capture duration.
    for (let frame = 1; frame < raw.length / 8; frame++) {
      const a = raw.readFloatLE((frame - 1) * 8);
      const b = raw.readFloatLE(frame * 8);
      if (a <= 0 && b > 0) crossings.push(frame - 1 - a / (b - a));
    }
    expect(crossings.length).toBeGreaterThan(50);
    const hz = ((crossings.length - 1) * 48_000) / (crossings.at(-1)! - crossings[0]!);
    const report = await pcm();
    fs.writeFileSync(path.join(root, `out/canopy-bend-${name}.json`), JSON.stringify({ hz, expected, ...report }));
    expect(report.non_finite).toBe(0);
    expect(report.rms).toBeGreaterThan(1e-4);
    expect(Math.abs(hz - expected)).toBeLessThan(0.2);
  };
  await session.client.call('midi.usb.in', { bytes: [0x90, 69, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([{ token: 1, gate: 1 }]);
  await measurePitch('center', 440);
  await session.client.call('midi.usb.in', { bytes: [0xe0, 127, 127] });
  const high = 440 * 2 ** (8191 / 4096 / 12);
  await measurePitch('up', high);
  await session.client.call('midi.in', { bytes: [0xe0, 0, 0] });
  await measurePitch('other-route', high);
  await session.client.call('midi.usb.in', { bytes: [0xb0, 121, 0] });
  await measurePitch('reset', 440);
  expect(await readVoices()).toEqual([{ token: 1, gate: 1 }]);
  await session.client.call('midi.usb.in', { bytes: [0x80, 69, 0] });
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([]);
  expect(await word(3)).toBe(0);
  await session.expectNoFault('CANOPY route pitch bend');
}, 240_000);
