import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions } from './devices.ts';
import { liveProtocol } from './protocol.ts';
import { expectRuntimeUi } from './ready.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { waitInstrumentReady } from './instrument-ready.ts';
import { acknowledgedPanel } from './panel-ack.ts';
import { loadPanelProject, savePanelProject } from './project-kit.ts';
import { sessionRt } from './port-compat.ts';

// Panel recording and note-paired absolute playback of a packed CANOPY lock
// (finding 347, stage 3). This uses a disposable emulator card and image.
vi.setConfig({ hookTimeout: 600_000 });

const options = liveOptions('dt2');
const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-lock-card-'));
const moduleFile = path.resolve(repoRoot(), process.env.CANOPY_LOCK_PACKAGE ?? 'out/patch/canopy-fm-release.dkm');
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
  if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-lock-card-')) {
    throw new Error('Refusing to remove a card fixture outside its owned directory');
  }
  fs.rmSync(owned, { recursive: true });
});
const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });
test('CANOPY plays and records trig locks', { timeout: 900_000 }, async () => {
  const root = repoRoot(),
    image = path.resolve(root, process.env.CANOPY_LOCK_IMAGE ?? 'out/patch/canopy-multi-release.syx');
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
  const peek = async (addr: number, len: number) => {
    const r = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr, len });
    expect(r.unknown).toBe(0);
    return Buffer.from(r.hex, 'hex');
  };
  const { tap: key, edge, turn } = acknowledgedPanel(session);
  const artifacts = path.join(root, 'out/canopy-lock');
  fs.mkdirSync(artifacts, { recursive: true });
  await expectRuntimeUi(session, plan, artifacts, { afterEpoch: beforeRestart, emulatedMs: 180_000 });
  const log: Record<string, unknown> = {};
  onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(log, null, 2)));
  const selectTrack = async (n: number) => {
    await edge('TRK', true);
    try {
      await key(`TRIG ${n}`);
    } finally {
      await edge('TRK', false);
    }
  };
  const chooseCanopy = async () => {
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
  await selectTrack(1);
  await chooseCanopy();
  await expect.poll(() => cf('ip_track'), { timeout: 60_000 }).toBe(0);
  await expect.poll(() => cf('ip_control'), { timeout: 60_000 }).toBe(3);
  // Grid recording: trigs on steps 1 and 5.
  await key('RECORD');
  await key('TRIG 1');
  await key('TRIG 5');
  await key('RECORD');

  // Lock storage (finding 347): the active pattern (*0x4316501c) holds 80
  // lanes of 0x102 bytes {index, track, 128 x s16} at +0x17e50, the lane map
  // at +0x50a0 (track * 71 + index), step counts at +0x5510 (track * 128 + step).
  const byteAt = async (a: number) => {
    const w = await word((a & ~3) >>> 0);
    return (w >>> ((3 - (a & 3)) * 8)) & 0xff;
  };
  const pattern = (await word(0x4316501c)) >>> 0;
  const locks = pattern + 0x17e50;
  // The earlier probe planted raw memory and did not prove panel editing.
  // Record the current draft's COLOR target using the real SRC and trig UI.
  await key('SRC');
  for (let i = 0; i < 5 && (await cf('ip_src_debug', 1)) !== 2; i++) await key('SRC');
  expect(await cf('ip_src_debug', 1)).toBe(2);
  const colorBefore = await cf('src_state', 8); // controls 2/3, big-endian
  await key('RECORD');
  await edge('TRIG 5', true);
  try {
    const refresh = await Promise.all(Array.from({ length: 8 }, (_, i) => cf('ip_src_refresh', i)));
    const view = refresh[3] ? await word(refresh[3]! + 8) : 0;
    const seq = view ? await word(view + 0x6c) : 0;
    log.heldBefore = {
      refresh,
      view,
      seq,
      held: seq ? await Promise.all(Array.from({ length: 4 }, (_, i) => word(seq + 0x258 + i * 4))) : [],
      src: await Promise.all(Array.from({ length: 12 }, (_, i) => cf('ip_src_debug', i))),
    };
    await turn(2, 20); // SOUND B = COLOR in the four-page layout
    // CvDisplay.locked follows the byte fields at offset 201. Encoder B's
    // COLOR lock must be visible while this step is held.
    await expect.poll(() => byteAt(planSymbol(plan, 'display') + 201), { timeout: 30_000 }).toBe(2);
    log.heldAfter = {
      src: await Promise.all(Array.from({ length: 12 }, (_, i) => cf('ip_src_debug', i))),
      state: await Promise.all(Array.from({ length: 12 }, (_, i) => cf('src_state', i))),
      lane: await byteAt(locks + 0x50a0 + 26),
    };
  } finally {
    await edge('TRIG 5', false);
  }
  log.afterTrigRelease = { lane: await byteAt(locks + 0x50a0 + 26), pattern: await word(0x4316501c) };
  await key('RECORD');
  const lane = await byteAt(locks + 0x50a0 + 26);
  log.afterRecord = { lane, pattern: await word(0x4316501c) };
  expect(lane, 'held turn allocates COLOR lock lane').toBeLessThan(80);
  const atStep5 = ((await byteAt(locks + lane * 0x102 + 10)) << 8) | (await byteAt(locks + lane * 0x102 + 11));
  expect(atStep5, 'step 5 contains a tagged packed lock').not.toBe(0xffff);
  expect(atStep5 & 3).toBe(1);
  expect(await cf('src_state', 8), 'held turn preserves base sound').toBe(colorBefore);
  log.panelLock = { lane, atStep5, colorBefore };
  console.info('recorded COLOR lock', log.panelLock);

  // Save the pattern with the tagged value, then verify its reload below.
  await savePanelProject(sessionRt(session));
  await key('SRC');
  for (let i = 0; i < 5 && (await cf('ip_src_debug', 1)) !== 2; i++) await key('SRC');
  expect(await cf('ip_src_debug', 1)).toBe(2);

  // The normal image enables capture; no debug memory writes are required.
  expect(await cf('ip_lock_enabled')).toBe(1);
  const slotAddr = planSymbol(plan, '_iv_lock_slots'),
    inputAddr = planSymbol(plan, '_iv_input');
  const banks = new Set(
    ['_iv_controls', '_iv_mod_controls', '_iv_lock_controls'].map((name) => planSymbol(plan, name)),
  );
  const seen = new Map<string, number>();
  await key('PLAY');
  const until = Date.now() + 30_000;
  while (Date.now() < until) {
    const step = await byteAt(0x431650ef);
    const slotResult = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', {
      addr: slotAddr,
      len: 8,
    });
    if (slotResult.unknown >= 8) continue; // slot has not been reset yet
    const slot = Buffer.from(slotResult.hex, 'hex');
    const input = await peek(inputAddr, 44);
    const bankAddress = input.readUInt32LE(36);
    if (!banks.has(bankAddress)) continue; // no processed block yet
    const bank = await peek(bankAddress, 76);
    const count = slot.readUInt32LE(0),
      pair = count ? slot.readUInt32LE(4) : 0,
      color = bank.readUInt32LE(72) & 65535;
    const k = `${step}:${count}:${pair}:${color}`;
    seen.set(k, (seen.get(k) ?? 0) + 1);
  }
  await key('STOP');
  log.captureDebug = await Promise.all(Array.from({ length: 12 }, (_, i) => cf('ip_lock_capture_debug', i)));
  log.captureError = await cf('ip_lock_capture_error');
  log.admission = await Promise.all(Array.from({ length: 8 }, (_, i) => cf('ip_admission', i)));
  log.played = Object.fromEntries(seen);
  console.info('packed COLOR playback', log.played, log.captureDebug, log.captureError);
  const samples = [...seen.keys()].map((value) => value.split(':').map(Number));
  expect(
    samples.some(([, count, pair, color]) => count === 1 && pair === ((2 << 10) | 84) && color === 84),
    'locked trig reaches COLOR as an absolute value',
  ).toBe(true);
  expect(
    samples.some(([, count, , color]) => count === 0 && color === 64),
    'an unlocked trig restores the base value',
  ).toBe(true);

  // The same held-step control clears the custom lock without editing Sound.
  await key('RECORD');
  await edge('TRIG 5', true);
  try {
    await key('PUSH B');
  } finally {
    await edge('TRIG 5', false);
  }
  await key('RECORD');
  const clearedLane = await byteAt(locks + 0x50a0 + 26);
  const clearedValue =
    clearedLane < 80
      ? ((await byteAt(locks + clearedLane * 0x102 + 10)) << 8) | (await byteAt(locks + clearedLane * 0x102 + 11))
      : 0xffff;
  log.cleared = { clearedLane, clearedValue };
  expect(clearedValue, 'pushing the held control clears the lock').toBe(0xffff);
  expect(await cf('src_state', 8), 'clearing preserves the base sound').toBe(colorBefore);

  await loadPanelProject(sessionRt(session), { health: false });
  const loadedPattern = (await word(0x4316501c)) >>> 0;
  const loadedLocks = loadedPattern + 0x17e50;
  const loadedLane = await byteAt(loadedLocks + 0x50a0 + 26);
  expect(loadedLane, 'project reload restores the CANOPY lock lane').toBeLessThan(80);
  const loadedValue =
    ((await byteAt(loadedLocks + loadedLane * 0x102 + 10)) << 8) |
    (await byteAt(loadedLocks + loadedLane * 0x102 + 11));
  log.reloaded = { loadedPattern, loadedLane, loadedValue };
  expect(loadedValue, 'project reload preserves the exact packed value').toBe(atStep5);

  const modified = async (modifier: string, name: string) => {
    await edge(modifier, true);
    try {
      await key(name);
    } finally {
      await edge(modifier, false);
    }
  };
  // Copy A01 to A02 while stopped; stock pattern operations must preserve the
  // tagged value on the destination as well as on return to the source.
  await modified('FUNC', 'RECORD');
  await modified('PTN', 'TRIG 2');
  await expect.poll(() => word(0x4316501c), { timeout: 60_000 }).not.toBe(loadedPattern);
  await modified('FUNC', 'STOP');
  const copiedPattern = (await word(0x4316501c)) >>> 0;
  const copiedLocks = copiedPattern + 0x17e50;
  const copiedLane = await byteAt(copiedLocks + 0x50a0 + 26);
  expect(copiedLane, 'pattern paste restores the CANOPY lock lane').toBeLessThan(80);
  const copiedValue =
    ((await byteAt(copiedLocks + copiedLane * 0x102 + 10)) << 8) |
    (await byteAt(copiedLocks + copiedLane * 0x102 + 11));
  expect(copiedValue, 'pattern paste preserves the packed value').toBe(atStep5);
  await modified('PTN', 'TRIG 1');
  await expect.poll(() => word(0x4316501c), { timeout: 60_000 }).toBe(loadedPattern);
  const returnedLane = await byteAt(loadedLocks + 0x50a0 + 26);
  expect(returnedLane).toBeLessThan(80);
  const returnedValue =
    ((await byteAt(loadedLocks + returnedLane * 0x102 + 10)) << 8) |
    (await byteAt(loadedLocks + returnedLane * 0x102 + 11));
  log.copied = { copiedPattern, copiedLane, copiedValue, returnedLane, returnedValue };
  expect(returnedValue, 'switching back preserves the source lock').toBe(atStep5);

  // Switch during playback and wait for the committed destination, not merely
  // the earlier lookahead selection. The copied step must still reach the DSP.
  const pos = (index: number) => cf('pp_position', index);
  await key('PLAY');
  await expect.poll(() => pos(5), { timeout: 60_000 }).toBe(3);
  await expect.poll(() => pos(6), { timeout: 60_000 }).toBe(loadedPattern);
  await modified('PTN', 'TRIG 2');
  await expect.poll(() => pos(6), { timeout: 120_000 }).toBe(copiedPattern);
  const capturesAtBoundary = await cf('ip_lock_capture_debug', 10);
  await expect.poll(() => cf('ip_lock_capture_debug', 10), { timeout: 120_000 }).toBeGreaterThan(capturesAtBoundary);
  await expect
    .poll(
      async () => {
        const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', {
          addr: slotAddr,
          len: 8,
        });
        if (result.unknown) return null;
        const value = Buffer.from(result.hex, 'hex');
        return [value.readUInt32LE(0), value.readUInt32LE(4)];
      },
      { timeout: 120_000 },
    )
    .toEqual([1, (2 << 10) | 84]);
  await modified('PTN', 'TRIG 1');
  await expect.poll(() => pos(6), { timeout: 120_000 }).toBe(loadedPattern);
  await key('STOP');
  expect(await cf('ip_lock_capture_error')).toBe(0);

  // Record a sweep from the panel while transport runs. The track's current
  // step receives a new absolute COLOR lock; the Sound remains unchanged.
  await key('SRC');
  for (let i = 0; i < 5 && (await cf('ip_src_debug', 1)) !== 2; i++) await key('SRC');
  expect(await cf('ip_src_debug', 1)).toBe(2);
  const liveBase = await cf('src_state', 8);
  await modified('RECORD', 'PLAY');
  await expect.poll(() => pos(5), { timeout: 60_000 }).toBe(3);
  await turn(2, 10);
  await key('STOP');
  log.liveState = {
    before: liveBase,
    after: await cf('src_state', 8),
    src: await Promise.all(Array.from({ length: 12 }, (_, i) => cf('ip_src_debug', i))),
  };
  expect(await cf('src_state', 8), 'live lock recording preserves the base sound').toBe(liveBase);
  const livePattern = (await word(0x4316501c)) >>> 0;
  const liveLocks = livePattern + 0x17e50;
  const liveLane = await byteAt(liveLocks + 0x50a0 + 26);
  expect(liveLane).toBeLessThan(80);
  const values: number[] = [];
  for (let step = 0; step < 128; step++) {
    const address = liveLocks + liveLane * 0x102 + 2 + 2 * step;
    const raw = ((await byteAt(address)) << 8) | (await byteAt(address + 1));
    if (raw !== 0xffff) values.push(raw);
  }
  log.live = { liveLane, values };
  expect(
    values.some((value) => value !== atStep5 && (value & 3) === 1),
    'live turn records a new lock',
  ).toBe(true);

  // A second CANOPY track has its own pattern lanes and DSP lock slot.
  const firstTrackValue =
    ((await byteAt(liveLocks + liveLane * 0x102 + 10)) << 8) | (await byteAt(liveLocks + liveLane * 0x102 + 11));
  await selectTrack(2);
  await chooseCanopy();
  await expect
    .poll(
      async () => {
        const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
        return tracks.includes(1);
      },
      { timeout: 60_000 },
    )
    .toBe(true);
  const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
  const secondSlot = tracks.indexOf(1);
  expect(secondSlot).toBeGreaterThanOrEqual(0);
  await expect.poll(() => cf('ip_admission', secondSlot * 8), { timeout: 60_000 }).toBe(1);
  await key('SRC');
  for (let i = 0; i < 5 && (await cf('ip_src_debug', 1)) !== 2; i++) await key('SRC');
  expect(await cf('ip_src_debug', 0)).toBe(1);
  expect(await cf('ip_src_debug', 1)).toBe(2);
  await key('RECORD');
  await key('TRIG 1');
  await edge('TRIG 1', true);
  try {
    await turn(2, 10);
  } finally {
    await edge('TRIG 1', false);
  }
  await key('RECORD');
  const secondPattern = (await word(0x4316501c)) >>> 0;
  const secondLocks = secondPattern + 0x17e50;
  const secondLane = await byteAt(secondLocks + 0x50a0 + 71 + 26);
  expect(secondLane, 'second track owns its own lock lane').toBeLessThan(80);
  const secondValue =
    ((await byteAt(secondLocks + secondLane * 0x102 + 2)) << 8) | (await byteAt(secondLocks + secondLane * 0x102 + 3));
  expect(secondValue).toBe(365); // COLOR 74 encoded as (17 + 74) * 4 + 1
  const firstLaneAfter = await byteAt(secondLocks + 0x50a0 + 26);
  expect(firstLaneAfter).toBeLessThan(80);
  const firstValueAfter =
    ((await byteAt(secondLocks + firstLaneAfter * 0x102 + 10)) << 8) |
    (await byteAt(secondLocks + firstLaneAfter * 0x102 + 11));
  expect(firstValueAfter, 'second track editing preserves first track lock').toBe(firstTrackValue);
  await key('PLAY');
  await expect
    .poll(
      async () => {
        const result = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', {
          addr: slotAddr + secondSlot * 96,
          len: 8,
        });
        if (result.unknown) return null;
        const value = Buffer.from(result.hex, 'hex');
        return [value.readUInt32LE(0), value.readUInt32LE(4)];
      },
      { timeout: 120_000 },
    )
    .toEqual([1, (2 << 10) | 74]);
  await key('STOP');
  log.secondTrack = { tracks, secondSlot, secondLane, secondValue, firstValueAfter };
});
