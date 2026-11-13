import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '@digi/regression/support.ts';
import { liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol, prepareRuntimePlanOffline, type RuntimePlan } from '@digi/regression/runtime/runtime-plan.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { openDigikitMenu } from '@digi/regression/runtime/runtime-menu.ts';
import { canopyControls } from '@digi/regression/runtime/canopy-controls.ts';
import { encoderValue } from '@digi/regression/runtime/encoder-value.ts';
import { waitEmulatedFrames } from '@digi/regression/runtime/waits.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';

// Full DTII 1.17 firmware, real panel input and MIDI, with a disposable card.
// Only stock tempo/receive settings are fixture inputs: no fabricated timing
// provider, instrument configuration or DSP state is written.
vi.setConfig({ hookTimeout: 600_000 });
const options = liveOptions('dt2');
const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-arp-card-'));
const moduleFile = process.env.CANOPY_ARP_PACKAGE
  ? path.resolve(repoRoot(), process.env.CANOPY_ARP_PACKAGE)
  : moduleArtifact('dt2', 'digikit/canopy');
beforeAll(() => {
  for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
    fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
  const modules = path.join(cardSeed, 'digikit', 'modules');
  fs.mkdirSync(modules, { recursive: true });
  fs.copyFileSync(moduleFile, path.join(modules, 'canopy.dkm'));
});
afterAll(() => {
  const owned = fs.realpathSync(cardSeed);
  if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-arp-card-'))
    throw new Error('Refusing to remove an unowned card fixture');
  fs.rmSync(owned, { recursive: true });
});
const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

test('CANOPY arp uses pattern tempo and plays held keys while stopped', { timeout: 900_000 }, async () => {
  const root = repoRoot();
  const { session } = h;
  const artifacts = path.join(root, 'out/canopy-pattern-arp', path.basename(session.runDir));
  fs.mkdirSync(artifacts, { recursive: true });
  console.info('CANOPY arp evidence:', artifacts);
  const log: Record<string, unknown> = {};
  onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(log, null, 2)));
  // The owner target builds the explicit core roots and the packed module.
  // The shared helper verifies generated-image identity before the daemon load.
  const prepared = process.env.CANOPY_ARP_IMAGE
    ? undefined
    : await prepareRuntimePlanOffline(
        session.options,
        session.runDir,
        artifacts,
        ['DKRT_BOOT', 'ip_control', 'src_state', '_ir_rows', '_iv_input'],
        { modules: ['core/instrument-play', 'core/digikit-menu', 'core/health'] },
      );
  const image = prepared?.syx ?? path.resolve(root, process.env.CANOPY_ARP_IMAGE!);
  const plan: RuntimePlan =
    prepared?.plan ?? JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
  expect(plan.base.device).toBe('dt2');
  expect(plan.base.version).toBe('1.17');
  expect(createHash('sha256').update(fs.readFileSync(image)).digest('hex')).toBe(plan.output?.syx_sha256);
  await session.client.firmwareLoad({
    syx: image,
    base_sha256: plan.base.syx_sha256,
    start: 'auto',
    force: true,
    auto_no: true,
    card: session.cardPath,
  });
  await session.client.call('run.mode', { mode: 'free' });
  const dk = liveProtocol(session);
  expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
  await dk.select(['modules/canopy.dkm']);
  const beforeRestart = (await session.status()).epoch;
  if (typeof beforeRestart !== 'number') throw new Error('Missing pre-restart epoch');
  await dk.restart();
  await dk.waitForBoot(undefined, 180_000);
  const word = async (address: number) => {
    const value = await session.client.memPeek(address);
    if (value === null) throw new Error(`Unreadable firmware word at ${address.toString(16)}`);
    return value >>> 0;
  };
  const cf = (name: string, i = 0) => word(planSymbol(plan, name) + 4 * i);
  const peek = async (addr: number, len: number) => {
    const r = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr, len });
    expect(r.unknown).toBe(0);
    return Buffer.from(r.hex, 'hex');
  };
  const stockWord = async (addr: number, value: number) => {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(value);
    await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
  };
  await waitInstrumentReady(cf);
  await expectRuntimeUi(session, plan, artifacts, { afterEpoch: beforeRestart, timeout: 180_000 });
  const { tap: key, edge, turn } = acknowledgedPanel(session);
  await edge('TRK', true);
  try {
    await key('TRIG 1');
  } finally {
    await edge('TRK', false);
  }
  await edge('FUNC', true);
  try {
    await key('SRC');
  } finally {
    await edge('FUNC', false);
  }
  for (let i = 0; i < 20; i++) await key('DOWN');
  await key('YES');
  await key('YES');
  await expect.poll(() => cf('ip_track'), { timeout: 60_000 }).toBe(0);
  await expect.poll(() => cf('ip_control'), { timeout: 60_000 }).toBe(3);
  const row = await peek(planSymbol(plan, '_ir_rows'), 64);
  expect(row.readUInt32LE(0)).toBe(3);
  const state = row.readUInt32LE(16); // admitted registry instance 0
  const bank = async () => {
    const b = await peek(planSymbol(plan, '_iv_controls'), 128);
    return Array.from({ length: 32 }, (_, i) => b.readUInt32LE(i * 4));
  };
  const { settled } = canopyControls(cf, bank);
  const srcPage = async (page: number) => {
    if (!(await cf('ip_src_debug', 2))) await key('SRC');
    await expect.poll(() => cf('ip_src_debug', 2), { timeout: 30_000 }).toBeGreaterThan(0);
    for (let i = 0; i < 4 && (await cf('ip_src_debug', 1)) !== page; i++) await key('SRC');
    expect(await cf('ip_src_debug', 1)).toBe(page);
  };
  await openDigikitMenu(key);
  await key('YES'); // CANOPY
  await key('YES'); // INPUT
  await key('YES'); // ENABLE
  await expect.poll(() => cf('ip_control', 1), { timeout: 30_000 }).toBe(1);
  for (let i = 0; i < 4; i++) await key('NO');
  await stockWord(0x429368ca, 3); // stock MIDI receive: USB and DIN
  await stockWord(0x42936872, 0); // stock receive channel fixture
  await stockWord(0x429368d6, 0); // internal tempo for this proof
  await srcPage(0);
  await encoderValue(
    (delta) => turn(7, delta),
    () => settled(5),
    127,
    { maxStep: 20 },
  );
  await srcPage(1);
  expect(await settled(6)).toBe(4); // 1/16
  expect(await settled(10)).toBe(0); // straight swing
  const beforePreview = await bank();
  const revision = await cf('src_state', 3);
  await turn(7, -30);
  await turn(8, 30);
  await edge('SRC', true);
  try {
    fs.writeFileSync(
      path.join(artifacts, 'arp-values.json'),
      JSON.stringify(await session.client.framebufferGet({ data: true, hash: true })),
    );
  } finally {
    await edge('SRC', false);
  }
  expect(await cf('src_state', 3), 'read-only G/H do not edit or publish configuration').toBe(revision);
  expect(await bank()).toEqual(beforePreview);
  expect(beforePreview[13]).toBe(2);
  console.info('CANOPY arp: the read-only preview preserves the configuration');
  await key('STOP');
  await expect.poll(() => word(0x43164f94), { timeout: 30_000 }).not.toBe(1);

  const midi = (bytes: number[]) => session.client.call('midi.usb.in', { bytes });
  const notes = [60, 64, 67];
  onTestFinished(async () => {
    for (const note of notes) await midi([0x80, note, 0]);
  });
  const arp = async () => {
    const b = await peek(state + 136 * 4, 35 * 4);
    return {
      token: b.readUInt32LE(0),
      phase: b.readUInt32LE(10 * 4),
      serial: b.readUInt32LE(13 * 4),
      note: b.readUInt32LE(15 * 4),
      block: b.readUInt32LE(29 * 4),
      mode: b.readUInt32LE(26 * 4),
      status: b.readUInt32LE(34 * 4),
    };
  };
  const inputFlags = async () => (await peek(planSymbol(plan, '_iv_input') + 48, 4)).readUInt32LE();
  const measureStopped = async (bpm: number) => {
    // 0x402b67fc is the stock current tempo, in BPM*120 (finding 78).
    await stockWord(0x402b67fc, bpm * 120);
    await expect.poll(async () => (await inputFlags()) >>> 16, { timeout: 30_000 }).toBe(bpm * 120);
    expect((await inputFlags()) & 0xd03).toBe(0xd00);
    const initialSerial = (await arp()).serial;
    await expect.poll(async () => (await arp()).serial, { timeout: 60_000 }).toBeGreaterThan(initialSerial + 1);
    const first = await arp();
    await expect.poll(async () => (await arp()).serial, { timeout: 90_000 }).toBeGreaterThanOrEqual(first.serial + 6);
    const last = await arp();
    expect(first.mode).toBe(2);
    expect(last.mode).toBe(2);
    expect(last.status).toBe(2);
    // Remove the in-progress remainder to compare actual onset times even if
    // a slow host skips several events between polls. Both fields share one
    // instance snapshot, avoiding a race with a separate daemon frame read.
    const increment = 2 * bpm * 120 * 4;
    const elapsed = last.block - first.block - (last.phase - first.phase) / increment;
    const expected = ((last.serial - first.serial) * 90000) / (bpm * 4);
    expect(Math.abs(elapsed - expected), `stopped ${bpm} BPM onset spacing`).toBeLessThanOrEqual(2);
    return { bpm, first, last, elapsed, expected };
  };
  await stockWord(0x402b67fc, 120 * 120);
  for (const note of notes) await midi([0x90, note, 100]);
  await expect.poll(async () => (await arp()).serial, { timeout: 60_000 }).toBeGreaterThan(2);
  log.stopped120 = await measureStopped(120);
  log.stopped180 = await measureStopped(180);
  console.info('CANOPY arp: stopped timing follows stock 120/180 BPM', log.stopped120, log.stopped180);
  expect(await cf('src_state', 3), 'tempo follows the pattern without editing the sound').toBe(revision);

  await key('PLAY');
  await expect.poll(() => word(0x43164f94), { timeout: 30_000 }).toBe(1);
  await expect.poll(async () => (await arp()).mode, { timeout: 90_000 }).toBe(1);
  const playing = await arp();
  await expect.poll(async () => (await arp()).serial, { timeout: 90_000 }).toBeGreaterThan(playing.serial + 3);
  log.playing = { before: playing, after: await arp(), flags: await inputFlags() };
  await key('STOP');
  await expect.poll(async () => (await arp()).mode, { timeout: 30_000 }).toBe(2);
  const stoppedAgain = await arp();
  await expect.poll(async () => (await arp()).serial, { timeout: 90_000 }).toBeGreaterThan(stoppedAgain.serial + 3);
  log.stoppedAgain = { before: stoppedAgain, after: await arp(), flags: await inputFlags() };
  console.info('CANOPY arp: held keyboard notes survive PLAY and STOP', log.playing, log.stoppedAgain);
  for (const note of notes) await midi([0x80, note, 0]);
  await expect.poll(async () => (await arp()).token, { timeout: 90_000 }).toBe(0);
  const released = (await arp()).serial;
  await waitEmulatedFrames(session, 375);
  expect((await arp()).serial, 'released notes stop generating arp events').toBe(released);
  expect(await cf('ip_control')).toBe(3);
  expect(await cf('mt_guard_fault')).toBe(0);
  await session.expectNoFault('CANOPY pattern-tempo arp across stopped/running transport');
  const health = await session.status();
  log.health = health;
  expect(health.cf?.error, JSON.stringify(health)).toBeNull();
});
