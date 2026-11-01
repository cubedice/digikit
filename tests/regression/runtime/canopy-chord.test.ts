import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { button, repoRoot } from '@digi/test-support';
import { expect, onTestFinished, test } from 'vitest';
import { withSession } from '../support.ts';
import { settleUi } from './live.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { chordWindow, voiceChange } from './chord-continuity.ts';

const h = withSession({
  device: 'dt2',
  firmware: '1.17',
  dsp: 'sharc',
  restart: false,
  binary: process.env.DIGI_EMUD_DT2,
});

test('held C and G preserve phase and gain while E is released', async () => {
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
  const capture = path.join(root, 'out/canopy-chord-continuity.f32le');
  await session.client.call('pcm.tap', { action: 'capture', path: capture });
  let capturing = true;
  onTestFinished(async () => {
    if (capturing) await session.client.call('pcm.tap', { action: 'capture_stop' });
  });
  const window = async () => {
    await session.client.call('pcm.tap', { action: 'start' });
    await expect
      .poll(
        async () => {
          const report = await session.client.call<{ samples: number; non_finite: number }>('pcm.tap', {
            action: 'report',
          });
          expect(report.non_finite).toBe(0);
          return report.samples;
        },
        { timeout: 30_000 },
      )
      .toBeGreaterThanOrEqual(48000);
    // The file can lag by a buffered write; the long settle interval keeps this
    // complete 8192-frame window clear of the attack/release transition.
    const end = fs.statSync(capture).size / 8;
    expect(end).toBeGreaterThanOrEqual(8192);
    return end - 8192;
  };
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 80, 0x90, 64, 100] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 1, gate: 1 },
    { token: 2, gate: 1 },
  ]);
  const ce = await window();
  await session.client.call('midi.in', { bytes: [0x90, 67, 127] });
  await expect.poll(readVoices, { timeout: 10_000 }).toEqual([
    { token: 1, gate: 1 },
    { token: 2, gate: 1 },
    { token: 3, gate: 1 },
  ]);
  const ceg = await window();
  await session.client.call('midi.usb.in', { bytes: [0x80, 64, 0] });
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([
    { token: 1, gate: 1 },
    { token: 3, gate: 1 },
  ]);
  const cg = await window();
  await session.client.call('pcm.tap', { action: 'capture_stop' });
  capturing = false;
  const raw = fs.readFileSync(capture);
  const before = chordWindow(raw, ce, 8192),
    added = chordWindow(raw, ceg, 8192),
    released = chordWindow(raw, cg, 8192);
  fs.writeFileSync(
    path.join(root, 'out/canopy-chord-continuity.json'),
    JSON.stringify({ ce, ceg, cg, before, added, released }),
  );
  for (const channel of [0, 1]) {
    expect(before[channel]![0]!.amplitude).toBeGreaterThan(1e-4);
    expect(added[channel]![2]!.amplitude).toBeGreaterThan(1e-4);
    expect(voiceChange(before[channel]![0]!, added[channel]![0]!)).toBeLessThan(0.001);
    expect(voiceChange(added[channel]![0]!, released[channel]![0]!)).toBeLessThan(0.001);
    expect(voiceChange(added[channel]![2]!, released[channel]![2]!)).toBeLessThan(0.001);
    expect(released[channel]![1]!.amplitude).toBeLessThan(added[channel]![1]!.amplitude * 0.001);
  }
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0] });
  await session.client.call('midi.in', { bytes: [0x80, 67, 0] });
  await expect.poll(readVoices, { timeout: 60_000 }).toEqual([]);
  expect(await word(3)).toBe(0);
  await session.expectNoFault('CANOPY held chord continuity');
}, 300_000);
