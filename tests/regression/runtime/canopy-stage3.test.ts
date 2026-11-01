import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { button, repoRoot } from '@digi/test-support';
import { expect, onTestFinished, test } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions, liveProtocol, settleUi } from './live.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { openDigikitMenu } from './runtime-menu.ts';
import { chordWindow } from './chord-continuity.ts';

const h = withSession({ ...liveOptions('dt2'), dsp: 'sharc', restart: false });
test('CANOPY Stage 3 live pad controls, stock filter and reverb', async () => {
  const root = repoRoot(),
    image = path.join(root, 'out/patch/canopy-stage3.syx');
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
    file = fs.readFileSync(path.join(root, 'out/patch/canopy-dt2.dkm'));
  await dk.put('modules/canopy.dkm', file);
  expect(await dk.verifyFile('modules/canopy.dkm', file)).toBe(true);
  await dk.select(['modules/canopy.dkm']);
  await dk.restart();
  await dk.waitForBoot(undefined, 180_000);
  const cf = (name: string, i = 0) => session.client.memPeek(planSymbol(plan, name) + 4 * i);
  const diagnostic = async () => {
    const values: Record<string, unknown> = {};
    for (const [name, n] of [
      ['DKRT_BOOT', 20],
      ['DKRT_INSTRUMENT_FILE', 4],
      ['DKRT_INSTRUMENT_SESSION', 4],
      ['mt_guard_fault', 5],
      ['ip_control', 8],
    ] as const)
      values[name] = await Promise.all(Array.from({ length: n }, (_, i) => cf(name, i)));
    for (const name of ['_ir_rows', '_ir_process_rows', '_iv_result', '_mi_heartbeat'])
      values[name] = await session.client.call('dsp.peek', {
        addr: planSymbol(plan, name),
        len: name === '_ir_rows' ? 64 : 32,
      });
    fs.writeFileSync(path.join(root, 'out/canopy-stage3-state.json'), JSON.stringify(values, null, 2));
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
  const voices = async () => {
    const b = await peek(state, 192);
    return Array.from({ length: 6 }, (_, i) => ({ token: b.readUInt32LE(i * 32), gate: b.readUInt32LE(i * 32 + 4) }))
      .filter((v) => v.token !== 0)
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
    path.join(root, 'out/canopy-stage3-chooser.json'),
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
  ] as const) {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(value);
    await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
  }
  const sound = async () => {
    const b = await peek(planSymbol(plan, '_iv_controls'), 24);
    return Array.from({ length: 6 }, (_, i) => b.readUInt32LE(i * 4));
  };
  const menuRow = async (row: number) => {
    for (let i = 0; i < 28; i++) await key('UP', 30);
    for (let i = 0; i < row; i++) await key('DOWN', 50);
  };
  const turn = async (enc: number, delta: number, count = 1) => {
    for (let i = 0; i < count; i++) {
      await session.client.inputTurn(enc, delta);
      await delay(250);
    }
  };
  const adjust = async (index: number, target: number) => {
    await expect
      .poll(
        async () => {
          const value = (await sound())[index]!;
          if (value !== target) await turn(1, Math.sign(target - value) * 4);
          return (await sound())[index];
        },
        { timeout: 90000, interval: 350 },
      )
      .toBe(target);
  };
  const sample = async (name: string) => {
    const capture = path.join(root, `out/canopy-stage3-${name}.f32le`);
    await session.client.call('pcm.tap', { action: 'capture', path: capture });
    await session.client.call('pcm.tap', { action: 'start' });
    await expect
      .poll(
        async () => {
          const r = await session.client.call<{ samples: number; non_finite: number }>('pcm.tap', { action: 'report' });
          expect(r.non_finite).toBe(0);
          return r.samples;
        },
        { timeout: 45000 },
      )
      .toBeGreaterThanOrEqual(24000);
    await session.client.call('pcm.tap', { action: 'capture_stop' });
    const raw = fs.readFileSync(capture);
    const n = Math.min(8192, raw.length / 8),
      start = raw.length / 8 - n;
    let energy = 0,
      peak = 0;
    for (let i = start * 2; i < raw.length / 4; i++) {
      const v = raw.readFloatLE(i * 4);
      expect(Number.isFinite(v)).toBe(true);
      energy += v * v;
      peak = Math.max(peak, Math.abs(v));
    }
    return { rms: Math.sqrt(energy / (n * 2)), peak, tones: chordWindow(raw, start, n, [60, 72, 79]) };
  };
  onTestFinished(async () => {
    await session.client.call('pcm.tap', { action: 'capture_stop' });
  });
  expect(await sound()).toEqual([3, 0, 64, 0, 5, 15]);
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 127] });
  await expect.poll(async () => (await voices()).length, { timeout: 15000 }).toBe(1);
  const ownership = await voices();
  const velvet = await sample('velvet');
  await openDigikitMenu(key);
  await key('YES');
  await menuRow(18);
  await key('YES');
  await expect.poll(async () => (await sound())[1], { timeout: 15000 }).toBe(1);
  await menuRow(19);
  await adjust(2, 80);
  expect(await voices()).toEqual(ownership);
  const reed = await sample('reed');
  for (let ch = 0; ch < 2; ch++) {
    expect(reed.tones[ch]![1]!.amplitude).toBeGreaterThan(velvet.tones[ch]![1]!.amplitude * 3 + 1e-5);
    expect(reed.tones[ch]![0]!.amplitude).toBeGreaterThan(1e-4);
  }
  await menuRow(20);
  await adjust(3, 16);
  expect(await voices()).toEqual(ownership);
  expect((await peek(state + 536, 4)).readFloatLE()).toBeGreaterThan(0);
  await adjust(3, 0);
  await menuRow(22);
  await adjust(5, 0);
  for (let i = 0; i < 3; i++) await key('NO');
  console.info('CANOPY Stage 3: live sound controls retained ownership');
  const dry = await sample('dry');
  await key('FLTR');
  let closed = dry;
  for (let attempt = 0; attempt < 6 && closed.rms >= dry.rms * 0.25; attempt++) {
    await turn(5, -8, 24);
    closed = await sample('filter-closed');
  }
  expect(closed.rms).toBeLessThan(dry.rms * 0.25);
  expect(await voices()).toEqual(ownership);
  let opened = closed;
  for (let attempt = 0; attempt < 6 && opened.rms < dry.rms * 0.75; attempt++) {
    await turn(5, 8, 24);
    opened = await sample('filter-open');
  }
  expect(opened.rms).toBeGreaterThan(closed.rms * 4);
  await key('FX');
  await turn(6, 8, 96);
  await sample('reverb-held');
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0] });
  await expect.poll(voices, { timeout: 30000 }).toEqual([]);
  const tail = await sample('reverb-tail');
  expect(tail.rms).toBeGreaterThan(1e-6);
  expect(await peek(planSymbol(plan, '_iv_output'), 128)).toEqual(Buffer.alloc(128));
  expect(await cf('ip_control', 4)).toBe(0);
  await session.expectNoFault('CANOPY Stage 3');
  await dk.info();
  fs.writeFileSync(
    path.join(root, 'out/canopy-stage3-audio.json'),
    JSON.stringify({ velvet, reed, dry, closed, opened, tail }),
  );
  console.info('CANOPY Stage 3: live family/color/drift/release, stock filter and reverb passed');
}, 900_000);
