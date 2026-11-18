import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions } from './devices.ts';
import { liveProtocol } from './protocol.ts';
import { expectRuntimeUi } from './ready.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { execFileSync } from 'node:child_process';
import { waitInstrumentReady } from './instrument-ready.ts';
import { acknowledgedPanel } from './panel-ack.ts';
import { seedModules } from './card-seed.ts';

// Sequencer trigs and trig parameter locks on a CANOPY track (finding 347).
// Asserts the stock lock facts the packed design relies on: a held turn of a
// stock knob records a lock that playback applies; locks on the machine
// slots CANOPY does not use play back unclamped, 0..0x7fff. Sequencer trigs
// play CANOPY notes and release them. The rest of CANOPY's behaviour is
// logged to out/canopy-plock/probe.json.
vi.setConfig({ hookTimeout: 600_000 });

const options = liveOptions('dt2');
const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-plock-card-'));
const moduleFile = path.resolve(repoRoot(), process.env.CANOPY_PLOCK_PACKAGE ?? 'out/patch/canopy-fm-release.dkm');
beforeAll(async () => {
  for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav']) {
    fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
  }
  await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines'] }]);
});
afterAll(() => {
  const owned = fs.realpathSync(cardSeed);
  if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-plock-card-')) {
    throw new Error('Refusing to remove a card fixture outside its owned directory');
  }
  fs.rmSync(owned, { recursive: true });
});
const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });
test('CANOPY track trig locks: stock lock storage and playback', { timeout: 900_000 }, async () => {
  const root = repoRoot(),
    image = path.resolve(root, process.env.CANOPY_PLOCK_IMAGE ?? 'out/patch/canopy-multi-release.syx');
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
  const row = await peek(planSymbol(plan, '_ir_rows'), 64);
  const state = row.readUInt32LE(16);
  const voices = async () => {
    const b = await peek(state, 192);
    return Array.from({ length: 6 }, (_, i) => ({
      token: b.readUInt32LE(i * 32),
      gate: b.readUInt32LE(i * 32 + 4),
    })).filter((v) => v.token !== 0);
  };
  const controls = async () => {
    const b = await peek(planSymbol(plan, '_iv_controls'), 128);
    return Array.from({ length: 32 }, (_, i) => b.readUInt32LE(i * 4));
  };
  const srcValues = async () => {
    // CsState: seven words, then value[32] as halfwords (CF is big-endian).
    const base = planSymbol(plan, 'src_state');
    const out: number[] = [];
    for (let i = 0; i < 16; i++) {
      const w = await word(base + 28 + 4 * i);
      out.push(w >>> 16, w & 0xffff);
    }
    return out;
  };
  const { tap: key, edge } = acknowledgedPanel(session);
  const artifacts = path.join(root, 'out/canopy-plock');
  fs.mkdirSync(artifacts, { recursive: true });
  await expectRuntimeUi(session, plan, artifacts, { afterEpoch: beforeRestart, emulatedMs: 180_000 });
  const log: Record<string, unknown> = {};
  const shot = (name: string) =>
    execFileSync(
      'uv',
      [
        'run',
        'python',
        '-m',
        'tools.control.digi_client',
        '--run-dir',
        session.runDir,
        'frame',
        path.join(artifacts, `${name}.png`),
      ],
      { cwd: root, timeout: 20_000 },
    );
  // Raw stock parameter row of track 1 (finding 345): 0x80003362 + 2 * index.
  const rawParam = async (index: number) => {
    const address = 0x80003362 + 2 * index;
    const w = await word((address & ~3) >>> 0);
    return address & 2 ? w & 0xffff : w >>> 16;
  };
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
  const srcPage = async (page: number) => {
    await key('SRC');
    for (let i = 0; i < 5 && (await cf('ip_src_debug', 1)) !== page; i++) await key('SRC');
    expect(await cf('ip_src_debug', 1)).toBe(page);
  };
  await srcPage(0); // SOUND: A TUNE and H LEV are stock parameters, B..G CANOPY's own
  shot('plock-src');

  // Grid recording: trigs on steps 1 and 5.
  await key('RECORD');
  await key('TRIG 1');
  await key('TRIG 5');
  shot('plock-grid');
  await key('RECORD');

  const watch = async (label: string, ms: number) => {
    const seen = new Map<number, number>();
    const controlSeen: number[][] = [];
    const end = Date.now() + ms;
    while (Date.now() < end) {
      for (const v of await voices()) seen.set(v.token, v.gate);
      const c = await controls();
      const last = controlSeen[controlSeen.length - 1];
      if (!last || last.some((x, i) => x !== c[i])) controlSeen.push(c);
      await delay(40);
    }
    log[label] = {
      tokens: [...seen.entries()],
      controlVariants: controlSeen.length,
      controls: controlSeen.slice(0, 6),
    };
    console.info(label, { tokens: seen.size, controlVariants: controlSeen.length });
  };
  const capture = path.join(artifacts, 'play.f32le');
  await session.client.call('pcm.tap', { action: 'capture', path: capture });
  await session.client.call('pcm.tap', { action: 'start' });
  await key('PLAY');
  await watch('play-trigs', 15_000);
  await key('STOP');
  const played = log['play-trigs'] as { tokens: [number, number][] };
  expect(played.tokens.length, 'sequencer trigs start CANOPY notes').toBeGreaterThan(0);
  await expect.poll(async () => (await voices()).filter((v) => v.gate === 1).length, { timeout: 30_000 }).toBe(0);
  await session.client.call('pcm.tap', { action: 'capture_stop' });
  const raw = fs.readFileSync(capture);
  let energy = 0;
  const frames = Math.floor(raw.length / 8);
  for (let f = 0; f < frames; f++) energy += raw.readFloatLE(f * 8) ** 2 + raw.readFloatLE(f * 8 + 4) ** 2;
  log.playRms = Math.sqrt(energy / Math.max(1, 2 * frames));
  log.playFrames = frames;
  console.info('play rms', log.playRms, frames);

  // Positive control: grid recording, hold step 5, turn the stock LEV knob (SOUND H).
  expect(await cf('ip_src_debug', 1)).toBe(0);
  log.levBefore = await rawParam(34);
  await key('RECORD');
  await edge('TRIG 5', true);
  try {
    await delay(300);
    for (let i = 0; i < 8; i++) {
      await session.client.inputTurn(8, -4);
      await delay(200);
    }
    shot('plock-held-lev');
  } finally {
    await edge('TRIG 5', false);
  }
  await key('RECORD');
  log.levAfterEdit = await rawParam(34);
  const levSeen = new Set<number>();
  await key('PLAY');
  const levEnd = Date.now() + 10_000;
  while (Date.now() < levEnd) {
    levSeen.add(await rawParam(34));
    await delay(20);
  }
  await key('STOP');
  log.levDuringPlay = [...levSeen];
  expect(log.levAfterEdit, 'a held turn records a lock, not a sound edit').toBe(log.levBefore);
  expect([...levSeen]).toContain(log.levBefore as number);
  expect(
    [...levSeen].some((v) => v < (log.levBefore as number)),
    'playback applies the lock',
  ).toBe(true);
  console.info('LEV raw before/after/play', log.levBefore, log.levAfterEdit, [...levSeen]);

  // Lock storage in RAM (DT2 1.17 static reading): the active pattern object
  // (*0x4316501c) holds its locks at +0x17e50: 80 lanes of 0x102 bytes
  // {runtime index, track, 128 x s16}, the lane map at +0x50a0
  // (track * 71 + index) and per-step counts at +0x5510 (track * 128 + step).
  const byteAt = async (a: number) => {
    const w = await word((a & ~3) >>> 0);
    return (w >>> ((3 - (a & 3)) * 8)) & 0xff;
  };
  const halfAt = async (a: number) => ((await byteAt(a)) << 8) | (await byteAt(a + 1));
  const write = (a: number, bytes: number[]) =>
    session.client.call('mem.cf.write', { addr: a >>> 0, hex: Buffer.from(bytes).toString('hex') });
  const pattern = (await word(0x4316501c)) >>> 0;
  const locks = pattern + 0x17e50;
  const laneOf = async (track: number, index: number) => {
    const b = await byteAt(locks + 0x50a0 + track * 71 + index);
    return b === 0xff ? -1 : b;
  };
  const levLane = await laneOf(0, 34);
  log.levLane = levLane;
  if (levLane >= 0) {
    const base = locks + levLane * 0x102;
    log.levLaneHeader = [await byteAt(base), await byteAt(base + 1)];
    log.levLaneSteps = [await halfAt(base + 2), await halfAt(base + 2 + 8), await halfAt(base + 2 + 10)];
  }
  console.info('LEV lane', log.levLane, log.levLaneHeader, log.levLaneSteps);
  // Plant packed-range locks on two CANOPY SRC slots the machine does not use:
  // index 31 = 0x7123 on step 5, index 26 = 5 on step 1.
  const free: number[] = [];
  for (let lane = 0; lane < 80 && free.length < 3; lane++)
    if ((await byteAt(locks + lane * 0x102)) === 0xff) free.push(lane);
  expect(free.length).toBe(3);
  const plant = async (lane: number, index: number, step: number, value: number) => {
    const base = locks + lane * 0x102;
    await write(base, [index, 0]);
    await write(base + 2 + step * 2, [value >> 8, value & 0xff]);
    await write(locks + 0x50a0 + index, [lane]);
    const count = await byteAt(locks + 0x5510 + step);
    await write(locks + 0x5510 + step, [count + 1]);
  };
  // 31 above the stock 0x7f00 ceiling on step 5; 26 small on step 5; 28 on step 1.
  await plant(free[0]!, 31, 4, 0x7fff);
  await plant(free[1]!, 26, 4, 5);
  await plant(free[2]!, 28, 0, 0x1234);
  const seen31 = new Set<number>(),
    seen26 = new Set<number>(),
    seen28 = new Set<number>();
  log.base31 = await rawParam(31);
  log.base26 = await rawParam(26);
  log.base28 = await rawParam(28);
  await key('PLAY');
  const plantEnd = Date.now() + 10_000;
  while (Date.now() < plantEnd) {
    seen31.add(await rawParam(31));
    seen26.add(await rawParam(26));
    seen28.add(await rawParam(28));
    await delay(10);
  }
  await key('STOP');
  log.planted31 = [...seen31];
  log.planted26 = [...seen26];
  log.planted28 = [...seen28];
  expect(levLane).toBeGreaterThanOrEqual(0);
  expect(log.levLaneHeader).toEqual([34, 0]);
  expect([...seen31], 'no clamp above the stock 0x7f00 ceiling').toContain(0x7fff);
  expect([...seen26], 'small values on another free slot').toContain(5);
  console.info('planted 31/26/28', log.base31, [...seen31], log.base26, [...seen26], log.base28, [...seen28]);

  // Trig lock attempt: grid recording, hold step 5, turn SOUND C.
  log.srcBefore = await srcValues();
  log.controlsBefore = await controls();
  await key('RECORD');
  await edge('TRIG 5', true);
  try {
    await delay(300);
    for (let i = 0; i < 6; i++) {
      await session.client.inputTurn(3, 1);
      await delay(200);
    }
    shot('plock-held-turn');
  } finally {
    await edge('TRIG 5', false);
  }
  await key('RECORD');
  await delay(1000);
  log.srcAfter = await srcValues();
  log.controlsAfter = await controls();
  console.info('src before/after', JSON.stringify(log.srcBefore), JSON.stringify(log.srcAfter));
  await key('PLAY');
  await watch('play-after-lock', 10_000);
  await key('STOP');
});
