import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { button, repoRoot } from '@digi/test-support';

import { expect, onTestFinished, test } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions } from './devices.ts';
import { liveProtocol } from './protocol.ts';
import { settleUi } from './waits.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { captureRemoteOled } from './remote.ts';
import { openDigikitMenu } from './runtime-menu.ts';

const h = withSession({ ...liveOptions('dt2'), dsp: 'sharc', restart: false });
test('CANOPY holds its voice bank through Overloop capture and active uploads', async () => {
  const root = repoRoot(),
    image = path.join(root, 'out/patch/canopy-stage6.syx');
  const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  expect(createHash('sha256').update(fs.readFileSync(image)).digest('hex')).toBe(plan.output?.syx_sha256);
  expect(plan.modules.some((m) => m.startsWith('test/canopy'))).toBe(false);
  const { session } = h;
  await session.client.firmwareLoad({
    syx: image,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
    card: session.cardPath,
  });
  await session.client.call('run.mode', { mode: 'free' });
  const dk = liveProtocol(session),
    file = fs.readFileSync(path.join(root, 'out/patch/canopy-stage6.dkm'));
  await dk.put('modules/canopy.dkm', file);
  expect(await dk.verifyFile('modules/canopy.dkm', file)).toBe(true);
  const loop = fs.readFileSync(path.join(root, 'out/patch/overloop-stage6.dkm'));
  await dk.put('modules/overloop.dkm', loop);
  expect(await dk.verifyFile('modules/overloop.dkm', loop)).toBe(true);
  await dk.select(['modules/canopy.dkm', 'modules/overloop.dkm']);
  await dk.restart();
  await dk.waitForBoot(undefined, 180_000);
  const word = async (address: number) => {
    const value = await session.client.memPeek(address);
    if (value === null) throw new Error(`Unreadable firmware word at ${address.toString(16)}`);
    return value;
  };
  const cf = (name: string, i = 0) => word(planSymbol(plan, name) + 4 * i);
  const diagnostic = async () => {
    const values: Record<string, unknown> = {};
    for (const [name, n] of [
      ['DKRT_BOOT', 20],
      ['DKRT_INSTRUMENT_FILE', 4],
      ['DKRT_INSTRUMENT_SESSION', 4],
      ['mt_guard_fault', 5],
      ['ip_control', 8],
      ['ip_src_debug', 12],
      ['ip_src_refresh', 8],
      ['src_state', 21],
      ['src_status', 160],
    ] as const)
      values[name] = await Promise.all(Array.from({ length: n }, (_, i) => cf(name, i)));
    for (const name of [
      '_ir_rows',
      '_ir_process_rows',
      '_iv_result',
      '_mi_heartbeat',
      '_iv_input',
      '_pattern_position_info',
    ])
      values[name] = await session.client.call('dsp.peek', {
        addr: planSymbol(plan, name),
        len: name === '_ir_rows' ? 64 : name === '_iv_input' || name === '_pattern_position_info' ? 80 : 32,
      });
    fs.writeFileSync(path.join(root, 'out/canopy-stage6-state.json'), JSON.stringify(values, null, 2));
    return values;
  };
  await expect
    .poll(
      async () => {
        const phase = await cf('DKRT_INSTRUMENT_FILE');
        if (phase === 9) throw new Error(JSON.stringify(await diagnostic()));
        return phase;
      },
      { timeout: 60_000 },
    )
    .toBe(3);

  expect((await dk.modules()).map((m) => m.name)).toContain('digikit/canopy');
  expect(await cf('ip_control', 1)).toBe(0); // routes always start disabled
  const peek = async (addr: number, len: number) => {
    const r = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr, len });
    expect(r.unknown).toBe(0);
    return Buffer.from(r.hex, 'hex');
  };
  const row = await peek(planSymbol(plan, '_ir_rows'), 64);
  expect(row.readUInt32LE(0)).toBe(3);
  const state = row.readUInt32LE(16);
  onTestFinished(async () => {
    await diagnostic();
    await captureRemoteOled(session, 'canopy-stage6-finish');
    fs.writeFileSync(
      path.join(root, 'out/canopy-stage6-instance.json'),
      JSON.stringify(await session.client.call('dsp.peek', { addr: state, len: 800 })),
    );
  });
  const voices = async () => {
    const b = await peek(state, 192);
    return Array.from({ length: 6 }, (_, i) => ({
      slot: i,
      token: b.readUInt32LE(i * 32),
      gate: b.readUInt32LE(i * 32 + 4),
    }))
      .filter((v) => v.token !== 0 && !(v.gate & 4))
      .sort((a, b) => a.token - b.token);
  };
  const key = async (name: string, settle = 500) => {
    await session.client.inputButton(button(name), true);
    await delay(200);
    await session.client.inputButton(button(name), false);
    await delay(settle);
  };
  await settleUi(session);
  // Select the admitted machine using the stock machine chooser.
  await session.client.inputButton(button('FUNC'), true);
  try {
    await key('SRC');
  } finally {
    await session.client.inputButton(button('FUNC'), false);
  }
  for (let i = 0; i < 20; i++) await key('DOWN', 100);
  fs.writeFileSync(
    path.join(root, 'out/canopy-stage6-chooser.json'),
    JSON.stringify(await session.client.framebufferGet({ data: true, hash: true })),
  );
  await key('YES');
  await key('YES');
  await openDigikitMenu(key);
  await key('YES'); // KEYBOARD is the first optional setup row
  await key('DOWN'); // skip the track heading
  await key('YES'); // ENABLE
  await expect.poll(() => cf('ip_control', 1), { timeout: 10_000 }).toBe(1);
  await expect.poll(() => cf('ip_control'), { timeout: 30_000 }).toBe(3);
  for (let i = 0; i < 3; i++) await key('NO');
  expect(await cf('ip_admission', 5)).toBe(await cf('ip_control', 7));
  // Stock receive-port settings are independent of the instrument route.
  // Set the emulator fixture to receive both ports, as in the carrier proofs.
  for (const [addr, value] of [
    [0x429368ca, 3],
    [0x42936872, 0],
    [0x429368d6, 1],
  ] as const) {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(value);
    await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
  }
  expect((await dk.modules()).map((m) => m.name).sort()).toEqual(['digikit/canopy', 'digikit/overloop']);
  const notes = [48, 55, 60, 64];
  for (const note of notes) await session.client.call('midi.usb.in', { bytes: [0x90, note, 100] });
  await expect.poll(async () => (await voices()).length, { timeout: 30000 }).toBe(4);
  const held = await voices();
  const loopWord = (i: number) => cf('loop_ui_snapshot', i);
  const turnUntil = async (enc: number, delta: number, reached: () => Promise<boolean>) => {
    for (let i = 0; i < 40 && !(await reached()); i++) {
      await session.client.inputTurn(enc, delta);
      await delay(250);
    }
    expect(await reached()).toBe(true);
  };
  const audio = async (name: string) => {
    const file = path.join(root, `out/canopy-stage6-${name}.f32le`);
    await session.client.call('pcm.tap', { action: 'capture', path: file });
    await session.client.call('pcm.tap', { action: 'start' });
    await expect
      .poll(
        async () => {
          const r = await session.client.call<{ samples: number; non_finite: number }>('pcm.tap', { action: 'report' });
          expect(r.non_finite).toBe(0);
          return r.samples;
        },
        { timeout: 60000 },
      )
      .toBeGreaterThanOrEqual(24000);
    await session.client.call('pcm.tap', { action: 'capture_stop' });
    const raw = fs.readFileSync(file);
    let energy = 0,
      peak = 0;
    const start = Math.max(0, raw.length - 8192 * 8);
    for (let i = start; i < raw.length; i += 4) {
      const value = raw.readFloatLE(i);
      expect(Number.isFinite(value)).toBe(true);
      energy += value * value;
      peak = Math.max(peak, Math.abs(value));
    }
    const rms = Math.sqrt(energy / ((raw.length - start) / 4));
    expect(rms).toBeGreaterThan(1e-5);
    expect(peak).toBeLessThan(1);
    return { rms, peak };
  };
  onTestFinished(async () => {
    await session.client.call('pcm.tap', { action: 'capture_stop' });
  });
  const live = await audio('live');
  await key('STOP');
  await session.client.inputButton(button('FUNC'), true);
  try {
    await key('FX');
  } finally {
    await session.client.inputButton(button('FUNC'), false);
  }
  for (let i = 0; i < 4; i++) await key('UP');
  for (let i = 0; i < 3; i++) await key('DOWN');
  await expect.poll(() => loopWord(0), { timeout: 15000 }).toBe(0x4f564c31);
  await turnUntil(3, 8, async () => (await loopWord(15)) >= 8);
  await turnUntil(1, 8, async () => (await loopWord(2)) === 3);
  await key('PLAY');
  await expect.poll(() => loopWord(2), { timeout: 15000, interval: 100 }).toBe(1);
  // Real runtime protocol transfer while the DSP records and four notes remain held.
  const payload = Buffer.from(Array.from({ length: 32768 }, (_, i) => (i * 29) & 255));
  await dk.put('stage6/transfer.bin', payload);
  expect(await dk.verifyFile('stage6/transfer.bin', payload)).toBe(true);
  await expect.poll(() => loopWord(2), { timeout: 180000, interval: 200 }).toBe(2);
  await key('STOP');
  expect(await voices()).toEqual(held);
  const frames = await loopWord(3);
  expect(frames).toBeGreaterThan(32);
  await turnUntil(3, -4, async () => (await loopWord(15)) <= 2);
  if (!(await loopWord(15))) await turnUntil(3, 1, async () => (await loopWord(15)) > 0);
  await expect.poll(() => loopWord(13), { timeout: 10000 }).toBeLessThan(frames);
  await turnUntil(2, 1, async () => (await loopWord(14)) > 0);
  await expect.poll(() => loopWord(12), { timeout: 10000 }).toBeGreaterThan(0);
  await turnUntil(4, 8, async () => (await loopWord(6)) >= 0x3f7f0000);
  const loopAudio = await audio('loop');
  for (const note of notes) await session.client.call('midi.usb.in', { bytes: [0x80, note, 0] });
  await expect.poll(async () => (await voices()).length, { timeout: 30000 }).toBe(0);
  const released = await audio('loop-after-release');
  expect(await dk.verifyFile('stage6/transfer.bin', payload)).toBe(true);
  await session.expectNoFault('CANOPY and Overloop with active uploads');
  fs.writeFileSync(
    path.join(root, 'out/canopy-stage6-coexistence.json'),
    JSON.stringify({ live, loopAudio, released, frames, held }, null, 2),
  );
}, 900000);
