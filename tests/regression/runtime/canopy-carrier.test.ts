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

test('live carrier renders held keys and retires each release independently', async () => {
  const root = repoRoot();
  const image = path.join(root, 'out/patch/canopy-live-carrier.syx');
  const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  expect(plan.modules).toContain('test/canopy-carrier@0.1.0');
  const sha = createHash('sha256').update(fs.readFileSync(image)).digest('hex');
  expect(plan.output?.syx_sha256).toBe(sha);
  const { session } = h;
  const loaded = await session.client.firmwareLoad({
    syx: image,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
  });
  expect(loaded.syx_sha256).toBe(sha);
  await session.client.call('run.mode', { mode: 'free' });
  const status = planSymbol(plan, 'cp_transport_proof');
  const word = (index: number) => session.client.memPeek(status + 4 * index);
  const banks = planSymbol(plan, '_cp_banks');
  const readVoices = async () => {
    const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr: banks, len: 256 });
    expect(result.unknown).toBe(0);
    const bytes = Buffer.from(result.hex, 'hex');
    return Array.from({ length: 4 }, (_, index) => ({
      token: bytes.readUInt32LE(index * 32),
      gate: bytes.readUInt32LE(index * 32 + 4),
    }))
      .filter((voice) => voice.token !== 0)
      .sort((a, b) => a.token - b.token);
  };
  await expect.poll(() => word(0), { timeout: 30_000, interval: 100 }).toBe(3);
  expect(await word(1)).toBe(1);
  expect(await word(4)).toBe(0);
  expect(await word(5)).toBe(1);
  expect(await word(10)).toBe(2);
  expect(await readVoices()).toEqual([]);
  const write = async (addr: number, value: number) => {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(value);
    await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
  };
  await write(0x429368ca, 3); // enable both receive ports in this emulator only
  await write(0x42936872, 0); // channel 1 is auto
  const bank = async () => {
    const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr: banks, len: 256 });
    expect(result.unknown).toBe(0);
    return Buffer.from(result.hex, 'hex');
  };
  await settleUi(session);
  const shot = async (name: string) =>
    fs.writeFileSync(
      path.join(root, `out/canopy-carrier-${name}.json`),
      JSON.stringify(await session.client.framebufferGet({ data: true, hash: true })),
    );
  await shot('before');
  const key = async (name: string) => {
    await session.client.inputButton(button(name), true);
    await delay(300);
    await session.client.inputButton(button(name), false);
    await delay(1200);
  };
  await session.client.inputButton(button('FUNC'), true);
  try {
    await key('SRC');
  } finally {
    await session.client.inputButton(button('FUNC'), false);
  }
  await shot('menu');
  await key('DOWN');
  await shot('choice');
  await key('YES');
  await key('YES');
  await shot('after');
  // One stock audition primes the stock type/configuration path.
  await key('TRIG 1');
  await expect
    .poll(
      async () => {
        const r = await session.client.call<{ hex: string }>('dsp.peek', { addr: 0x255970, len: 4 });
        return Buffer.from(r.hex, 'hex').readUInt16LE();
      },
      { timeout: 10_000 },
    )
    .toBe(1);
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 80, 0x90, 64, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 1, gate: 1 },
    { token: 2, gate: 1 },
  ]);
  console.log(
    'carrier diagnostics',
    await session.client.call('dsp.peek', { addr: 0x255970, len: 8 }),
    await session.client.call('dsp.peek', { addr: planSymbol(plan, '_dv_diag'), len: 64 }),
  );
  const initial = await bank();
  await expect.poll(async () => (await bank()).equals(initial), { timeout: 10_000 }).toBe(false);
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0] });
  // Rendering must retire C while E continues, without another stock note trigger.
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([{ token: 2, gate: 1 }]);
  await session.client.call('midi.in', { bytes: [0x90, 67, 127] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 2, gate: 1 },
    { token: 3, gate: 1 },
  ]);
  await session.client.call('pcm.tap', { action: 'start' });
  await session.client.call('midi.usb.in', { bytes: [0x90, 64, 0] });
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([{ token: 3, gate: 1 }]);
  const pcm = await session.client.call<{ samples: number; nonzero: number; non_finite: number; rms: number }>(
    'pcm.tap',
    { action: 'report' },
  );
  fs.writeFileSync(path.join(root, 'out/canopy-carrier-held-pcm.json'), JSON.stringify(pcm));
  expect(pcm.samples).toBeGreaterThan(0);
  expect(pcm.non_finite).toBe(0);
  expect(pcm.nonzero, 'final output contains held notes').toBeGreaterThan(0);
  await session.client.call('midi.in', { bytes: [0x80, 67, 0] });
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([]);
  const active = planSymbol(plan, '_mi_carrier_active');
  await expect
    .poll(
      async () => {
        const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', {
          addr: active,
          len: 4,
        });
        expect(result.unknown).toBe(0);
        return Buffer.from(result.hex, 'hex').readUInt32LE();
      },
      { timeout: 10_000 },
    )
    .toBe(0);
  expect(await word(3)).toBe(0);
  await session.expectNoFault('CANOPY carrier lifecycle');
}, 240_000);
