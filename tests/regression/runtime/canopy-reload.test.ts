import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { button, repoRoot } from '@digi/test-support';
import { expect, test } from 'vitest';
import { withSession } from '../support.ts';
import { settleUi } from './live.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';

const h = withSession({
  device: 'dt2',
  firmware: '1.17',
  dsp: 'sharc',
  restart: false,
  binary: process.env.DIGI_EMUD_DT2,
});

test('temporary pattern reload invalidates the same Sound before fresh MIDI admission', async () => {
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
  const admission = planSymbol(plan, 'cp_admission');
  const word = (i: number) => session.client.memPeek(status + 4 * i);
  const info = (i: number) => session.client.memPeek(admission + 4 * i);
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
  const shot = async (name: string) =>
    fs.writeFileSync(
      path.join(root, `out/canopy-reload-${name}.json`),
      JSON.stringify(await session.client.framebufferGet({ data: true, hash: true })),
    );
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
  const source = await info(1);
  const generation = (await word(10))!;
  const loads = (await info(7))!;
  const refreshes = (await info(2))!;
  await modified('FUNC', 'YES');
  await shot('saved');
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([{ token: 1, gate: 1 }]);
  await modified('FUNC', 'NO');
  await shot('reloaded');
  fs.writeFileSync(
    path.join(root, 'out/canopy-reload-state.json'),
    JSON.stringify({
      source,
      afterSource: await info(1),
      generation,
      afterGeneration: await word(10),
      loads,
      afterLoads: await info(7),
      refreshes,
      afterRefreshes: await info(2),
    }),
  );
  await expect.poll(() => info(7), { timeout: 15_000 }).toBeGreaterThan(loads);
  await expect.poll(() => word(10), { timeout: 15_000 }).toBe(generation + 1);
  await expect.poll(readVoices, { timeout: 15_000 }).toEqual([]);
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  expect(await info(1)).toBe(source);
  expect(await info(6)).toBe(0);
  expect(await info(5)).toBe(1);
  expect(await info(2)).toBe(refreshes + 1);
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100, 0x80, 60, 0, 0x90, 67, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 2, gate: 1 },
    { token: 3, gate: 1 },
  ]);
  await session.client.call('pcm.tap', { action: 'start' });
  const pcm = () =>
    session.client.call<{ samples: number; rms: number; peak: number; non_finite: number }>('pcm.tap', {
      action: 'report',
    });
  await expect.poll(async () => (await pcm()).samples, { timeout: 20_000 }).toBeGreaterThanOrEqual(24_000);
  const held = await pcm();
  fs.writeFileSync(path.join(root, 'out/canopy-reload-held-pcm.json'), JSON.stringify(held));
  expect(held.rms).toBeGreaterThan(1e-4);
  expect(held.non_finite).toBe(0);
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0, 0x80, 67, 0] });
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([]);
  // Drain downstream residue before measuring a separate quiet window.
  await session.client.call('pcm.tap', { action: 'start' });
  await expect.poll(async () => (await pcm()).samples, { timeout: 20_000 }).toBeGreaterThanOrEqual(24_000);
  await session.client.call('pcm.tap', { action: 'start' });
  await expect.poll(async () => (await pcm()).samples, { timeout: 15_000 }).toBeGreaterThanOrEqual(4096);
  const quiet = await pcm();
  fs.writeFileSync(path.join(root, 'out/canopy-reload-quiet-pcm.json'), JSON.stringify(quiet));
  expect(quiet.non_finite).toBe(0);
  expect(quiet.rms).toBeLessThan(1e-6);
  expect(quiet.peak).toBeLessThan(1e-6);
  expect(await word(3)).toBe(0);
  await session.expectNoFault('CANOPY in-place reload');
}, 180_000);
