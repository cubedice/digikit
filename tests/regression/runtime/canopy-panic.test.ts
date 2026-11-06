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

test('channel-mode messages preserve other routes and stale-release ownership', async () => {
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
  const generation = await word(10);
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100, 0xb0, 64, 127] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([{ token: 1, gate: 1 }]);
  await session.client.call('midi.in', { bytes: [0x90, 67, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 1, gate: 1 },
    { token: 2, gate: 1 },
  ]);
  // All Notes Off honors sustain. The E onset proves preceding USB messages
  // reached the consumer; it also leaves a second physical key for panic.
  await session.client.call('midi.usb.in', { bytes: [0xb0, 123, 0, 0x90, 64, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 1, gate: 1 },
    { token: 2, gate: 1 },
    { token: 3, gate: 1 },
  ]);
  await session.client.call('midi.usb.in', { bytes: [0xb0, 120, 0] });
  await expect.poll(readVoices, { timeout: 15_000 }).toEqual([{ token: 2, gate: 1 }]);
  // Old C/E physical releases are tombstones. They cannot release new C.
  // A is an ordered barrier after both releases.
  await session.client.call('midi.usb.in', {
    bytes: [0x90, 60, 100, 0x80, 60, 0, 0x80, 64, 0, 0x90, 69, 100],
  });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 2, gate: 1 },
    { token: 4, gate: 1 },
    { token: 5, gate: 1 },
  ]);
  // Panic cleared USB sustain: All Notes Off now uses normal release.
  await session.client.call('midi.usb.in', { bytes: [0xb0, 123, 0] });
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([{ token: 2, gate: 1 }]);
  const pcm = () =>
    session.client.call<{ samples: number; rms: number; peak: number; non_finite: number }>('pcm.tap', {
      action: 'report',
    });
  await session.client.call('pcm.tap', { action: 'start' });
  await expect.poll(async () => (await pcm()).samples, { timeout: 20_000 }).toBeGreaterThanOrEqual(24_000);
  const held = await pcm();
  fs.writeFileSync(path.join(root, 'out/canopy-panic-din-held-pcm.json'), JSON.stringify(held));
  expect(held.non_finite).toBe(0);
  expect(held.rms).toBeGreaterThan(1e-4);
  await session.client.call('midi.in', { bytes: [0xb0, 120, 0] });
  await expect.poll(readVoices, { timeout: 15_000 }).toEqual([]);
  await session.client.call('pcm.tap', { action: 'start' });
  await expect.poll(async () => (await pcm()).samples, { timeout: 20_000 }).toBeGreaterThanOrEqual(24_000);
  await session.client.call('pcm.tap', { action: 'start' });
  await expect.poll(async () => (await pcm()).samples, { timeout: 15_000 }).toBeGreaterThanOrEqual(4096);
  const quiet = await pcm();
  fs.writeFileSync(path.join(root, 'out/canopy-panic-quiet-pcm.json'), JSON.stringify(quiet));
  expect(quiet.non_finite).toBe(0);
  expect(quiet.rms).toBeLessThan(1e-6);
  expect(quiet.peak).toBeLessThan(1e-6);
  expect(await word(10)).toBe(generation);
  expect(await word(0)).toBe(3);
  expect(await word(3)).toBe(0);
  await session.expectNoFault('CANOPY route panic');
}, 240_000);
