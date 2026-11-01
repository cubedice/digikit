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
import { chordWindow, voiceChange } from './chord-continuity.ts';

const h = withSession({ ...liveOptions('dt2'), dsp: 'sharc', restart: false });
test('admitted CANOPY keyboard, stereo chord and fault retirement', async () => {
  const root = repoRoot(),
    image = path.join(root, 'out/patch/canopy-stage1.syx');
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
    fs.writeFileSync(path.join(root, 'out/canopy-stage1-state.json'), JSON.stringify(values, null, 2));
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
    path.join(root, 'out/canopy-stage1-chooser.json'),
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
  let capture = path.join(root, 'out/canopy-stage1.f32le');
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
          const r = await session.client.call<{ samples: number; non_finite: number }>('pcm.tap', { action: 'report' });
          expect(r.non_finite).toBe(0);
          return r.samples;
        },
        { timeout: 45_000 },
      )
      .toBeGreaterThanOrEqual(48000);
    const end = fs.statSync(capture).size / 8;
    expect(end).toBeGreaterThanOrEqual(8192);
    return end - 8192;
  };
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 40, 0x90, 64, 100] });
  await expect.poll(voices, { timeout: 15_000 }).toEqual([
    { token: 1, gate: 1 },
    { token: 2, gate: 1 },
  ]);
  const ce = await window();
  await session.client.call('midi.in', { bytes: [0x90, 67, 127] });
  await expect.poll(voices, { timeout: 15_000 }).toEqual([
    { token: 1, gate: 1 },
    { token: 2, gate: 1 },
    { token: 3, gate: 1 },
  ]);
  const ceg = await window();
  await session.client.call('midi.usb.in', { bytes: [0x80, 64, 0] });
  await expect.poll(voices, { timeout: 60_000 }).toEqual([
    { token: 1, gate: 1 },
    { token: 3, gate: 1 },
  ]);
  const cg = await window();
  await session.client.call('pcm.tap', { action: 'capture_stop' });
  capturing = false;
  const raw = fs.readFileSync(capture),
    before = chordWindow(raw, ce, 8192),
    added = chordWindow(raw, ceg, 8192),
    released = chordWindow(raw, cg, 8192);
  fs.writeFileSync(
    path.join(root, 'out/canopy-stage1-audio.json'),
    JSON.stringify({ ce, ceg, cg, before, added, released }),
  );
  for (const channel of [0, 1]) {
    expect(before[channel]![0]!.amplitude).toBeGreaterThan(1e-4);
    expect(added[channel]![2]!.amplitude).toBeGreaterThan(added[channel]![0]!.amplitude);
    expect(voiceChange(before[channel]![0]!, added[channel]![0]!)).toBeLessThan(0.001);
    expect(voiceChange(added[channel]![0]!, released[channel]![0]!)).toBeLessThan(0.001);
    expect(voiceChange(added[channel]![2]!, released[channel]![2]!)).toBeLessThan(0.001);
    expect(released[channel]![1]!.amplitude).toBeLessThan(added[channel]![1]!.amplitude * 0.001);
  }
  // Inspect every transition sample, including the onset/release windows that
  // settled spectral fits exclude. Missing blocks and abrupt carrier restarts
  // must not hide between those fits.
  for (const channel of [0, 1]) {
    const tones = added[channel]!;
    const slope = tones.reduce(
      (sum, t) => sum + t.amplitude * ((2 * Math.PI * 440 * 2 ** ((t.note - 69) / 12)) / 48000 + 1 / 3840),
      0,
    );
    let zeros = 0,
      maxZeros = 0,
      nonFinite = 0,
      previous = raw.readFloatLE(ce * 8 + channel * 4),
      jump = 0;
    for (let frame = ce + 1; frame < cg + 8192; frame++) {
      const sample = raw.readFloatLE(frame * 8 + channel * 4);
      if (!Number.isFinite(sample)) nonFinite++;
      jump = Math.max(jump, Math.abs(sample - previous));
      previous = sample;
      zeros = sample === 0 ? zeros + 1 : 0;
      maxZeros = Math.max(maxZeros, zeros);
    }
    expect(nonFinite).toBe(0);
    expect(maxZeros, 'no missing stereo blocks through note transitions').toBeLessThan(8);
    expect(jump, 'sample continuity through onset and release').toBeLessThan(slope * 1.25 + 1e-5);
  }
  // A pedal release affects the released key only, never the held other port.
  await session.client.call('midi.usb.in', { bytes: [0xb0, 64, 127, 0x80, 60, 0] });
  await delay(1500);
  expect(await voices()).toEqual([
    { token: 1, gate: 1 },
    { token: 3, gate: 1 },
  ]);
  await session.client.call('midi.usb.in', { bytes: [0xb0, 64, 0] });
  await expect.poll(voices, { timeout: 60_000 }).toEqual([{ token: 3, gate: 1 }]);
  // Repeat the same pitch at higher velocity while the other port stays held.
  capture = path.join(root, 'out/canopy-stage1-velocity.f32le');
  await session.client.call('pcm.tap', { action: 'capture', path: capture });
  capturing = true;
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 127] });
  await expect.poll(voices, { timeout: 15_000 }).toEqual([
    { token: 3, gate: 1 },
    { token: 4, gate: 1 },
  ]);
  const highStart = await window();
  await session.client.call('pcm.tap', { action: 'capture_stop' });
  capturing = false;
  const high = chordWindow(fs.readFileSync(capture), highStart, 8192);
  for (const channel of [0, 1]) {
    const ratio = high[channel]![0]!.amplitude / before[channel]![0]!.amplitude;
    expect(ratio).toBeGreaterThan(3.1);
    expect(ratio).toBeLessThan(3.25);
    expect(Math.abs(high[channel]![2]!.amplitude / added[channel]![2]!.amplitude - 1)).toBeLessThan(0.01);
  }
  expect(await cf('ip_control', 4)).toBe(0);
  await session.expectNoFault('admitted CANOPY keyboard');
  // Emulator-only corruption exercises the actual callback's rejection path.
  // The protected dispatcher must silence it, revoke routes and detach before
  // the coordinator releases the DSP allocation. CF code stays until reboot.
  await session.client.call('dsp.patch', { text: `DM ${(state + 512).toString(16)} ffffffff` });
  await expect.poll(() => cf('DKRT_INSTRUMENT_FILE'), { timeout: 60_000 }).toBe(6);
  expect((await peek(planSymbol(plan, '_ir_rows'), 4)).readUInt32LE()).toBe(6);
  expect(await cf('ip_control', 2)).toBe(0);
  expect(await peek(planSymbol(plan, '_iv_output'), 128)).toEqual(Buffer.alloc(128));
  await dk.info(); // unrelated USB control remains responsive after retirement
  await session.expectNoFault('admitted CANOPY fault retirement');
}, 600_000);
