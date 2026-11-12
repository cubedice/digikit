import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot, withinRuntimeDeadline } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions } from './devices.ts';
import { liveProtocol } from './protocol.ts';
import { expectRuntimeUi } from './ready.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { captureRemoteOled } from './remote.ts';
import { openDigikitMenu } from './runtime-menu.ts';
import { waitInstrumentReady } from './instrument-ready.ts';
import { acknowledgedPanel } from './panel-ack.ts';

// CANOPY on any track: the adapter admits the lowest kit track whose Sound
// carries the instrument type, owns that track's SRC page and panel keys,
// renders audio there, and rebinds when the instrument moves.
vi.setConfig({ hookTimeout: 600_000 });

const options = liveOptions('dt2');
const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-track-card-'));
const moduleFile = path.resolve(
  repoRoot(),
  process.env.CANOPY_TRACK_PACKAGE ?? 'out/patch/canopy-any-track-release.dkm',
);
beforeAll(() => {
  for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav']) {
    fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
  }
  const modules = path.join(cardSeed, 'digikit', 'modules');
  fs.mkdirSync(modules, { recursive: true });
  fs.copyFileSync(moduleFile, path.join(modules, 'canopy.dkm'));
});
afterAll(() => {
  const owned = fs.realpathSync(cardSeed);
  if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-track-card-')) {
    throw new Error('Refusing to remove a card fixture outside its owned directory');
  }
  fs.rmSync(owned, { recursive: true });
});
const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });
test('CANOPY is admitted, displayed and audible on track 11, then follows the instrument to track 1', async () => {
  const root = repoRoot(),
    image = path.resolve(root, process.env.CANOPY_TRACK_IMAGE ?? 'out/patch/canopy-any-track-release.syx');
  const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  expect(createHash('sha256').update(fs.readFileSync(image)).digest('hex')).toBe(plan.output?.syx_sha256);
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
    file = fs.readFileSync(moduleFile);
  expect(await dk.verifyFile('modules/canopy.dkm', file)).toBe(true);
  await dk.select(['modules/canopy.dkm']);
  const beforeRestart = (await session.status()).epoch;
  if (typeof beforeRestart !== 'number') throw new Error('Missing pre-restart epoch');
  await dk.restart();
  await dk.waitForBoot(undefined, 180_000);
  const word = async (address: number) => {
    const value = await session.client.memPeek(address);
    if (value === null) throw new Error(`Unreadable firmware word at ${address.toString(16)}`);
    return value;
  };
  const cf = (name: string, i = 0) => word(planSymbol(plan, name) + 4 * i);
  await waitInstrumentReady(cf);
  expect((await dk.modules()).map((m) => m.name)).toContain('digikit/canopy');
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
      .filter((v) => v.token !== 0 && !(v.gate & 4))
      .sort((a, b) => a.token - b.token);
  };
  const dspTrack = async () => (await peek(planSymbol(plan, '_iv_track'), 4)).readUInt32LE();
  const { tap: key, edge } = acknowledgedPanel(session);
  const artifacts = path.join(root, 'out/canopy-any-track');
  fs.mkdirSync(artifacts, { recursive: true });
  await expectRuntimeUi(session, plan, artifacts, { afterEpoch: beforeRestart, timeout: 180_000 });
  const dump = async (label: string) => {
    const values: Record<string, unknown> = {};
    for (const [name, n] of [
      ['ip_control', 8],
      ['ip_admission', 8],
      ['ip_track', 1],
      ['ip_src_debug', 12],
      ['ip_panel_debug', 8],
      ['ip_keyboard_status', 12],
      ['mi_transport', 48],
      ['mt_guard_fault', 5],
      ['frame_block', 1],
    ] as const)
      values[name] = await withinRuntimeDeadline(Date.now() + 20_000, () =>
        Promise.all(Array.from({ length: n }, (_, i) => cf(name, i))),
      ).catch((error: unknown) => ({ error: String(error) }));
    for (const [name, len] of [
      ['_ir_rows', 64],
      ['_mi_instances', 128],
      ['_mi_heartbeat', 32],
      ['_iv_input', 96],
      ['_iv_result', 16],
      ['_iv_output', 128],
      ['_iv_active', 4],
      ['_iv_track', 4],
      ['_iv_revision', 4],
      ['_mi_native_owner_track', 4],
      ['_mi_native_owner_valid', 4],
      ['_mi_native_owner_stamp', 4],
      ['_mi_post_owner_valid', 4],
      ['_mi_carrier_stamp', 4],
      ['_mi_carrier_pairs', 192],
      ['_mi_carrier_active', 64],
      ['_mi_velocity_owners', 128],
      ['_instrument_voice_rows', 16],
    ] as const)
      values[name] = await Promise.resolve()
        .then(() => session.client.call('dsp.peek', { addr: planSymbol(plan, name), len }))
        .catch((error: unknown) => ({ error: String(error) }));
    fs.writeFileSync(path.join(artifacts, `${label}.json`), JSON.stringify(values, null, 2));
  };
  onTestFinished(() => dump('state'));
  const selectTrack = async (n: number) => {
    await edge('TRK', true);
    try {
      await key(`TRIG ${n}`);
    } finally {
      await edge('TRK', false);
    }
  };
  const chooseCanopy = async () => {
    // The stock machine chooser lists CANOPY last for the selected track.
    await edge('FUNC', true);
    try {
      await key('SRC');
    } finally {
      await edge('FUNC', false);
    }
    for (let i = 0; i < 20; i++) await key('DOWN');
    await key('YES');
    await key('YES');
  };
  const srcOwnership = async (track: number) => {
    await selectTrack(track + 1);
    await key('SRC');
    await expect.poll(() => cf('ip_src_debug'), { timeout: 30_000 }).toBe(track);
    return cf('ip_src_debug', 2);
  };

  // No track carries the instrument: nothing is admitted or bound.
  expect(await cf('ip_track')).toBe(0xffffffff);
  expect(await cf('ip_control')).toBe(1);

  await selectTrack(11);
  await chooseCanopy();
  await expect.poll(() => cf('ip_track'), { timeout: 60_000 }).toBe(10);
  await expect.poll(() => cf('ip_control'), { timeout: 60_000 }).toBe(3);
  expect(await cf('ip_admission', 5)).toBe(await cf('ip_control', 7));
  expect(await dspTrack()).toBe(10);
  // The native SRC page belongs to track 11 and only there.
  expect(await srcOwnership(10)).not.toBe(0);
  await captureRemoteOled(session, 'canopy-track-11-src');
  expect(await srcOwnership(0)).toBe(0);
  await captureRemoteOled(session, 'canopy-track-1-stock');
  expect(await srcOwnership(10)).not.toBe(0);
  console.info('CANOPY any-track: admitted and displayed on track 11');

  // Panel keys on the instrument's track sound through the keyboard bridge.
  await key('KEYBOARD');
  const heldPanel = async () => (await voices()).filter((v) => v.gate === 1);
  try {
    const presses = await cf('ip_panel_debug');
    await edge('TRIG 9', true);
    await expect.poll(() => cf('ip_panel_debug'), { timeout: 30_000 }).toBe(presses + 1);
    expect(await cf('ip_panel_debug', 3)).toBe(10);
    await expect.poll(async () => (await heldPanel()).length, { timeout: 30_000 }).toBe(1);
  } finally {
    await edge('TRIG 9', false);
  }
  await expect.poll(async () => (await heldPanel()).length, { timeout: 30_000 }).toBe(0);
  await key('KEYBOARD');

  // External MIDI reaches the same track and is audible at the final output.
  await openDigikitMenu(key);
  await key('YES'); // CANOPY setup
  await key('YES'); // INPUT
  await key('YES'); // ENABLE
  await expect.poll(() => cf('ip_control', 1), { timeout: 10_000 }).toBe(1);
  await expect.poll(() => cf('ip_control'), { timeout: 30_000 }).toBe(3);
  for (let i = 0; i < 4; i++) await key('NO');
  for (const [addr, value] of [
    [0x429368ca, 3],
    [0x42936872, 0],
    [0x429368d6, 1],
  ] as const) {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(value);
    await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
  }
  const capture = path.join(artifacts, 'track-11.f32le');
  await session.client.call('pcm.tap', { action: 'capture', path: capture });
  let capturing = true;
  onTestFinished(async () => {
    if (capturing) await session.client.call('pcm.tap', { action: 'capture_stop' });
  });
  await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100, 0x90, 67, 100] });
  await expect.poll(async () => (await voices()).length, { timeout: 30_000 }).toBe(2);
  await session.client.call('pcm.tap', { action: 'start' });
  await expect
    .poll(
      async () => {
        const r = await session.client.call<{ samples: number; non_finite: number }>('pcm.tap', { action: 'report' });
        expect(r.non_finite).toBe(0);
        return r.samples;
      },
      { timeout: 60_000 },
    )
    .toBeGreaterThanOrEqual(48_000);
  await session.client.call('pcm.tap', { action: 'capture_stop' });
  capturing = false;
  const raw = fs.readFileSync(capture);
  const frames = Math.floor(raw.length / 8);
  let energy = 0;
  for (let frame = Math.max(0, frames - 8192); frame < frames; frame++) {
    const l = raw.readFloatLE(frame * 8),
      r = raw.readFloatLE(frame * 8 + 4);
    energy += l * l + r * r;
  }
  const rms = Math.sqrt(energy / (2 * Math.min(frames, 8192)));
  expect(rms, 'track 11 renders the held chord').toBeGreaterThan(1e-4);
  await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0, 0x80, 67, 0] });
  await expect.poll(async () => (await voices()).length, { timeout: 60_000 }).toBe(0);
  console.info('CANOPY any-track: panel and USB notes sound on track 11');

  // A second CANOPY on a lower track moves the single instance there.
  await selectTrack(1);
  await chooseCanopy();
  await expect.poll(() => cf('ip_track'), { timeout: 60_000 }).toBe(0);
  await expect.poll(() => cf('ip_control'), { timeout: 60_000 }).toBe(3);
  await expect.poll(dspTrack, { timeout: 30_000 }).toBe(0);
  expect(await srcOwnership(0)).not.toBe(0);
  expect(await srcOwnership(10)).toBe(0);
  await captureRemoteOled(session, 'canopy-track-11-after-move');
  await session.client.call('midi.usb.in', { bytes: [0x90, 64, 100] });
  await expect.poll(async () => (await voices()).length, { timeout: 30_000 }).toBe(1);
  await session.client.call('midi.usb.in', { bytes: [0x80, 64, 0] });
  await expect.poll(async () => (await voices()).length, { timeout: 60_000 }).toBe(0);
  expect(await cf('ip_control', 4)).toBe(0);
  await session.expectNoFault('CANOPY on any track');
  console.info('CANOPY any-track: instance followed the instrument to track 1');
}, 900_000);
