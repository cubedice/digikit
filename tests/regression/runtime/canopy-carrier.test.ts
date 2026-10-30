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
  // The investigation addresses below are pinned to this exact stock image.
  expect(plan.base.syx_sha256).toBe('26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5');
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
  await expect.poll(() => word(0), { timeout: 30_000, interval: 100 }).toBe(1);
  await expect.poll(() => word(1), { timeout: 10_000 }).toBe(1);
  await expect.poll(() => word(4), { timeout: 10_000 }).toBe(0);
  expect(await word(5)).toBe(0);
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
  const pcmReport = () =>
    session.client.call<{ samples: number; nonzero: number; non_finite: number; rms: number; peak: number }>(
      'pcm.tap',
      { action: 'report' },
    );
  const quietWindow = async (name: string) => {
    await session.client.call('pcm.tap', { action: 'start' });
    await expect.poll(async () => (await pcmReport()).samples, { timeout: 15_000 }).toBeGreaterThanOrEqual(4096);
    const pcm = await pcmReport();
    fs.writeFileSync(path.join(root, `out/canopy-carrier-${name}-pcm.json`), JSON.stringify(pcm));
    expect(pcm.non_finite).toBe(0);
    // Final stock DSP output can retain tiny residue, unlike the exact-zero kernel.
    // Require RMS and every sample below -120 dBFS, both before and after playing.
    expect(pcm.rms).toBeLessThan(1e-6);
    expect(pcm.peak).toBeLessThan(1e-6);
  };
  const settleResetOutput = async () => {
    // A 64-sample kernel reset can leave downstream residue. Drain a fixed
    // quarter-second of stereo output, then apply the unchanged quiet bound.
    await session.client.call('pcm.tap', { action: 'start' });
    await expect.poll(async () => (await pcmReport()).samples, { timeout: 30_000 }).toBeGreaterThanOrEqual(24_000);
    expect((await pcmReport()).non_finite).toBe(0);
  };
  await settleUi(session);
  await quietWindow('idle');
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
  const selectMachine = async (direction: 'UP' | 'DOWN') => {
    await session.client.inputButton(button('FUNC'), true);
    try {
      await key('SRC');
    } finally {
      await session.client.inputButton(button('FUNC'), false);
    }
    await shot('menu');
    await key(direction);
    await shot('choice');
    await key('YES');
    await key('YES');
  };
  await selectMachine('DOWN');
  await shot('after');
  const admissionSnapshot = async () => {
    const byte = async (addr: number) => {
      const value = await session.client.memPeek(addr - (addr % 4));
      expect(value).not.toBeNull();
      return (value! >>> (8 * (3 - (addr % 4)))) & 255;
    };
    // Stock refresh caches its source pointer and copies source+0xa2 to this row.
    const source = await session.client.memPeek(0x8000470c);
    const validSource = source !== null && source >= 0x40000000 && source < 0x48000000;
    const mirror = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', {
      addr: 0x255970,
      len: 4,
    });
    expect(mirror.unknown).toBe(0);
    return {
      source,
      sourceType: validSource ? await byte(source! + 0xa2) : null,
      rowType: await byte(0x80003cd0),
      dspType: Buffer.from(mirror.hex, 'hex').readUInt16LE(),
    };
  };
  // The fixture resolves and refreshes the selected Sound; no stock note trigger.
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  expect(await word(5)).toBe(1);
  await expect
    .poll(
      async () => {
        const r = await session.client.call<{ hex: string }>('dsp.peek', { addr: 0x255970, len: 4 });
        return Buffer.from(r.hex, 'hex').readUInt16LE();
      },
      { timeout: 10_000 },
    )
    .toBe(1);
  const admitted = await admissionSnapshot();
  fs.writeFileSync(path.join(root, 'out/canopy-carrier-admission.json'), JSON.stringify({ admitted }));
  expect(admitted.rowType).toBe(1);
  expect(admitted.sourceType).toBe(1);
  const admission = planSymbol(plan, 'cp_admission');
  expect(await session.client.memPeek(admission)).toBe(1);
  expect(await session.client.memPeek(admission + 4)).toBe(admitted.source);
  expect(await session.client.memPeek(admission + 8)).toBe(1);
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
  // Drain the stock output pipeline before observing a fresh silence window.
  await session.client.call('pcm.tap', { action: 'start' });
  await expect.poll(async () => (await pcmReport()).samples, { timeout: 15_000 }).toBeGreaterThanOrEqual(4096);
  await quietWindow('silence');

  // Replace the machine while a physical key is still down. Reset must close
  // routing and retire that generation without erasing its release tombstone.
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([{ token: 4, gate: 1 }]);
  await selectMachine('UP'); // WERP -> ONESHOT
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(1);
  await expect.poll(() => word(4), { timeout: 10_000 }).toBe(0);
  expect(await word(10)).toBe(3);
  expect(await word(5)).toBe(0);
  await expect.poll(readVoices, { timeout: 15_000 }).toEqual([]);
  expect(await session.client.memPeek(admission)).toBe(0);
  expect(await session.client.memPeek(admission + 16)).toBe(1);
  await settleResetOutput();
  await quietWindow('machine-reset');

  await selectMachine('DOWN'); // Re-admit the same track with a fresh generation.
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  expect(await word(10)).toBe(3);
  expect(await word(5)).toBe(1);
  expect(await session.client.memPeek(admission + 8)).toBe(2);
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([{ token: 5, gate: 1 }]);
  // G is an ordered USB barrier: its arrival proves the preceding old C-off
  // was consumed without releasing the newly pressed C of the same pitch.
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0, 0x90, 67, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 5, gate: 1 },
    { token: 6, gate: 1 },
  ]);

  const previousPattern = await session.client.memPeek(admission + 12);
  await session.client.inputButton(button('PTN'), true);
  try {
    await key('TRIG 2'); // Pattern selection, not a note audition.
  } finally {
    await session.client.inputButton(button('PTN'), false);
  }
  await expect.poll(() => session.client.memPeek(admission + 12), { timeout: 15_000 }).not.toBe(previousPattern);
  await expect.poll(() => word(10), { timeout: 15_000 }).toBe(4);
  await expect.poll(() => word(4), { timeout: 10_000 }).toBe(0);
  await expect.poll(readVoices, { timeout: 15_000 }).toEqual([]);
  expect(await session.client.memPeek(admission + 16)).toBe(2);
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0, 0x80, 67, 0] });
  await settleResetOutput();
  await quietWindow('pattern-reset');
  expect(await word(0)).toBe(1); // The empty destination pattern uses ONESHOT.
  await selectMachine('DOWN');
  await expect.poll(() => word(0), { timeout: 15_000 }).toBe(3);
  expect(await word(10)).toBe(4);
  expect(await word(5)).toBe(1);
  expect(await session.client.memPeek(admission + 8)).toBe(3);
  await session.client.call('pcm.tap', { action: 'start' });
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([{ token: 7, gate: 1 }]);
  await expect.poll(async () => (await pcmReport()).nonzero, { timeout: 15_000 }).toBeGreaterThan(0);
  expect((await pcmReport()).non_finite).toBe(0);
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0] });
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([]);
  await settleResetOutput();
  await quietWindow('readmitted');
  fs.writeFileSync(
    path.join(root, 'out/canopy-carrier-readmission.json'),
    JSON.stringify({
      generation: await word(10),
      phase: await word(0),
      pattern: await session.client.memPeek(admission + 12),
      refreshes: await session.client.memPeek(admission + 8),
      invalidations: await session.client.memPeek(admission + 16),
    }),
  );
  expect(await word(3)).toBe(0);
  await session.expectNoFault('CANOPY carrier lifecycle');
}, 360_000);
