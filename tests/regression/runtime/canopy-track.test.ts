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
import { waitEmulated } from './waits.ts';
import { menuSelection } from './menu-selection.ts';

// CANOPY on any track, several at once (finding 121): each of the four lowest
// kit tracks whose Sound carries the instrument type gets its own instance,
// SRC page, panel keys, MIDI route and audio; a fifth stays stock and silent.
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
test('CANOPY runs on any track, and up to four instances play at once', async () => {
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
  expect(row.readUInt32LE(12), 'the row admits four instances').toBe(4);
  // Registry instance i (DSP slot i) keeps its state at base + i * 2976.
  const voicesOf = async (instance: number) => {
    const b = await peek(state + instance * 2976, 192);
    return Array.from({ length: 6 }, (_, i) => ({ token: b.readUInt32LE(i * 32), gate: b.readUInt32LE(i * 32 + 4) }))
      .filter((v) => v.token !== 0 && !(v.gate & 4))
      .sort((a, b) => a.token - b.token);
  };
  const voices = () => voicesOf(0);
  const NONE = 0xffffffff;
  const tracks = () => Promise.all([0, 1, 2, 3].map((i) => cf('ip_tracks', i)));
  const phases = () => Promise.all([0, 1, 2, 3].map((i) => cf('ip_control', i * 8)));
  // IsStatus is 638 bytes on the two-byte-aligned ColdFire ABI: 632 bytes
  // of words, five validity bytes, and one byte of trailing padding.
  const statusOf = async (slot: number) => {
    const base = planSymbol(plan, 'src_status') + slot * 638;
    return {
      track: await word(base + 4),
      block: await word(base + 16),
      reportedTrack: (await word(base + 20)) & 15,
      valid: (await word(base + 632)) >>> 24,
    };
  };
  const expectLiveStatus = async (slot: number, track: number) => {
    await expect
      .poll(() => statusOf(slot), { timeout: 30_000 })
      .toMatchObject({ track, reportedTrack: track, valid: 1 });
    const first = await statusOf(slot);
    await waitEmulated(session, 500);
    const second = await statusOf(slot);
    expect(second).toMatchObject({ track, reportedTrack: track, valid: 1 });
    expect(second.block, `track ${track + 1} status stream advances`).toBeGreaterThan(first.block);
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
      ['_mi_native_owner', 64],
      ['_mi_post_ptr', 64],
      ['_iv_slot_tracks', 16],
      ['_iv_focus', 4],
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
  const waitInstrumentTicks = () => waitEmulated(session, 1500);
  const srcOwnership = async (track: number) => {
    await selectTrack(track + 1);
    await key('SRC');
    await expect.poll(() => cf('ip_src_debug'), { timeout: 30_000 }).toBe(track);
    return cf('ip_src_debug', 2);
  };

  // No track carries the instrument: nothing is admitted or bound.
  expect(await cf('ip_track')).toBe(0xffffffff);
  expect(await phases(), 'every instance is free and idle').toEqual([0, 0, 0, 0]);

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

  // A second CANOPY on a lower track adds a second instance; track 11 keeps the first.
  await selectTrack(1);
  await chooseCanopy();
  await expect.poll(tracks, { timeout: 60_000 }).toEqual([10, 0, NONE, NONE]);
  await expect.poll(phases, { timeout: 60_000 }).toEqual([3, 3, 0, 0]);
  expect(await srcOwnership(0)).not.toBe(0);
  expect(await srcOwnership(10)).not.toBe(0);
  expect(await srcOwnership(1)).toBe(0);
  await captureRemoteOled(session, 'canopy-multi-track-1-src');
  console.info('CANOPY multi: tracks 11 and 1 admitted together');

  // Both play at once: USB channel 1 stays with track 11 (it was routed
  // first), while the panel keys play the selected track 1.
  const pairs = async () => {
    const b = await peek(planSymbol(plan, '_mi_carrier_pairs'), 192);
    return [0, 10].map((t) => b.readUInt32LE((3 * t + 2) * 4));
  };
  const multiCapture = path.join(artifacts, 'multi.f32le');
  await selectTrack(1);
  await key('KEYBOARD');
  let load: unknown;
  try {
    await edge('TRIG 9', true);
    await expect.poll(async () => (await voicesOf(1)).filter((v) => v.gate === 1).length, { timeout: 30_000 }).toBe(1);
    await session.client.call('midi.usb.in', { bytes: [0x90, 60, 100, 0x90, 64, 100, 0x90, 67, 100] });
    await expect.poll(async () => (await voicesOf(0)).length, { timeout: 30_000 }).toBe(3);
    expect((await voicesOf(1)).filter((v) => v.gate === 1).length, 'track 1 still holds its panel note').toBe(1);
    // Both tracks' voices rendered through their own slots in the same frame.
    await expect.poll(pairs, { timeout: 30_000 }).toEqual([1, 1]);
    await expectLiveStatus(1, 0);
    await session.client.call('pcm.tap', { action: 'capture', path: multiCapture });
    capturing = true;
    await session.client.call('pcm.tap', { action: 'start' });
    await expect
      .poll(
        async () => {
          const r = await session.client.call<{ samples: number; non_finite: number }>('pcm.tap', {
            action: 'report',
          });
          expect(r.non_finite).toBe(0);
          return r.samples;
        },
        { timeout: 60_000 },
      )
      .toBeGreaterThanOrEqual(48_000);
    await session.client.call('pcm.tap', { action: 'capture_stop' });
    capturing = false;
    load = ((await session.status()) as { load?: unknown }).load;
  } finally {
    await edge('TRIG 9', false);
    await session.client.call('midi.usb.in', { bytes: [0x80, 60, 0, 0x80, 64, 0, 0x80, 67, 0] });
  }
  await key('KEYBOARD');
  const multiRaw = fs.readFileSync(multiCapture);
  const multiFrames = Math.floor(multiRaw.length / 8);
  let multiEnergy = 0;
  for (let frame = Math.max(0, multiFrames - 8192); frame < multiFrames; frame++) {
    const l = multiRaw.readFloatLE(frame * 8),
      r = multiRaw.readFloatLE(frame * 8 + 4);
    multiEnergy += l * l + r * r;
  }
  const multiRms = Math.sqrt(multiEnergy / (2 * Math.min(multiFrames, 8192)));
  expect(multiRms, 'two instances render at once').toBeGreaterThan(1e-4);
  fs.writeFileSync(path.join(artifacts, 'multi-load.json'), JSON.stringify({ multiRms, load }, null, 2));
  console.info('CANOPY multi: tracks 11 and 1 sounded together', { multiRms, load });
  await expect.poll(async () => (await voicesOf(0)).length + (await voicesOf(1)).length, { timeout: 60_000 }).toBe(0);

  // Two more instances; a fifth CANOPY above them stays stock and silent.
  for (const [n, expected] of [
    [5, [10, 0, 4, NONE]],
    [7, [10, 0, 4, 6]],
  ] as const) {
    await selectTrack(n);
    await chooseCanopy();
    await expect.poll(tracks, { timeout: 60_000 }).toEqual(expected);
  }
  await expect.poll(phases, { timeout: 60_000 }).toEqual([3, 3, 3, 3]);
  await selectTrack(13);
  await chooseCanopy();
  await waitInstrumentTicks();
  expect(await tracks()).toEqual([10, 0, 4, 6]);
  expect(await srcOwnership(12)).toBe(0);
  await captureRemoteOled(session, 'canopy-multi-fifth-stock');
  for (const t of [0, 4, 6, 10]) expect(await srcOwnership(t), `track ${t + 1}`).not.toBe(0);

  // The DSP status focus follows the selected CANOPY track.
  const focus = async () => (await peek(planSymbol(plan, '_iv_focus'), 4)).readUInt32LE();
  await selectTrack(7);
  await key('SRC');
  await expect.poll(focus, { timeout: 30_000 }).toBe(3);
  await expectLiveStatus(3, 6);
  await selectTrack(11);
  await key('SRC');
  await expect.poll(focus, { timeout: 30_000 }).toBe(0);
  await expectLiveStatus(0, 10);
  // Loading a preset resets one instance while other configurations and the
  // status stream are active. Every track must become playable again.
  const menu = menuSelection(session, plan, 'core/instrument-play', key);
  await openDigikitMenu(key);
  await key('YES');
  await menu('menu', 3);
  await key('YES');
  await menu('preset_menu', 2);
  await key('YES');
  await expect.poll(async () => (await cf('preset_present')) >>> 24, { timeout: 30_000 }).toBe(1);
  await waitEmulated(session, 1000); // longer than the bounded status FIFO
  await menu('preset_menu', 3);
  await key('YES');
  await expect.poll(phases, { timeout: 60_000 }).toEqual([3, 3, 3, 3]);
  for (let i = 0; i < 4; i++) await key('NO');
  await selectTrack(11);
  await key('KEYBOARD');
  try {
    await edge('TRIG 9', true);
    await expect.poll(async () => (await voicesOf(0)).filter((v) => v.gate === 1).length, { timeout: 30_000 }).toBe(1);
  } finally {
    await edge('TRIG 9', false);
  }
  await key('KEYBOARD');
  expect(await cf('ip_control', 4)).toBe(0);
  await session.expectNoFault('CANOPY on several tracks');
  console.info('CANOPY multi: four instances admitted, fifth stock, focus follows selection');
}, 1_200_000);
