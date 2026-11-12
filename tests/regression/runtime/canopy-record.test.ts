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

// Recording keyboard presses on a CANOPY track (finding 123, stage 2). While
// the sequencer live records, CANOPY hands KEYBOARD presses to the stock note
// action: the stock path records them as trigs and auditions them as
// sequencer trigs, which the frame path plays on CANOPY. Playback then plays
// the recorded notes on CANOPY. Outside recording the panel path keeps them.
vi.setConfig({ hookTimeout: 600_000 });

const options = liveOptions('dt2');
const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-record-card-'));
const moduleFile = path.resolve(repoRoot(), process.env.CANOPY_RECORD_PACKAGE ?? 'out/patch/canopy-fm-release.dkm');
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
  if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-record-card-')) {
    throw new Error('Refusing to remove a card fixture outside its owned directory');
  }
  fs.rmSync(owned, { recursive: true });
});
const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });
test('CANOPY records keyboard presses', { timeout: 900_000 }, async () => {
  const root = repoRoot(),
    image = path.resolve(root, process.env.CANOPY_RECORD_IMAGE ?? 'out/patch/canopy-multi-release.syx');
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
  // Voice bank (voice.s): {token, gate, x, y, sin(w), cos(w), gain, envelope}.
  const voices = async () => {
    const b = await peek(state, 192);
    return Array.from({ length: 6 }, (_, i) => ({
      token: b.readUInt32LE(i * 32),
      gate: b.readUInt32LE(i * 32 + 4),
      hz: (Math.atan2(b.readFloatLE(i * 32 + 16), b.readFloatLE(i * 32 + 20)) * 48_000) / (2 * Math.PI),
    })).filter((v) => v.token !== 0);
  };
  const { tap: key, edge } = acknowledgedPanel(session);
  const artifacts = path.join(root, 'out/canopy-record');
  fs.mkdirSync(artifacts, { recursive: true });
  await expectRuntimeUi(session, plan, artifacts, { afterEpoch: beforeRestart, timeout: 180_000 });
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
  const semitone = (hz: number) => 69 + 12 * Math.log2(hz / 440);
  // CANOPY voices started while watching: token -> pitch in semitones.
  const watch = async (label: string, ms: number) => {
    const seen = new Map<number, number>();
    const end = Date.now() + ms;
    while (Date.now() < end) {
      for (const v of await voices()) if (v.gate === 1 && v.hz > 0) seen.set(v.token, semitone(v.hz));
      await delay(30);
    }
    const notes = [...new Set([...seen.values()].map((n) => Math.round(n * 10) / 10))].sort((a, b) => a - b);
    log[label] = { tokens: seen.size, notes };
    console.info(label, log[label]);
    return notes;
  };
  const near = (notes: number[], n: number) => notes.some((m) => Math.abs(m - n) < 0.3);

  // Outside recording a KEYBOARD press stays on the CANOPY panel path.
  await key('KEYBOARD');
  const panelBefore = await cf('ip_panel_debug', 0);
  await key('TRIG 9');
  await expect.poll(() => cf('ip_panel_debug', 0), { timeout: 30_000 }).toBe(panelBefore + 1);
  expect(await cf('ip_panel_recorded')).toBe(0);

  // Live recording (RECORD + PLAY): two presses of different keys.
  await edge('RECORD', true);
  try {
    await key('PLAY');
  } finally {
    await edge('RECORD', false);
  }
  await delay(1_500); // the "Live Rec" message takes the first presses
  shot('record-live');
  const heard = new Map<number, number>();
  const listen = (async () => {
    const until = Date.now() + 4_000;
    while (Date.now() < until) {
      for (const v of await voices()) if (v.gate === 1 && v.hz > 0) heard.set(v.token, semitone(v.hz));
      await delay(30);
    }
  })();
  await key('TRIG 9');
  await delay(700);
  await key('TRIG 12');
  await listen;
  const recordedNotes = [...new Set([...heard.values()].map((n) => Math.round(n * 10) / 10))].sort((a, b) => a - b);
  log.recording = { tokens: heard.size, notes: recordedNotes, handed: await cf('ip_panel_recorded') };
  console.info('recording', log.recording);
  expect(await cf('ip_panel_recorded'), 'live-recorded presses go to the stock note action').toBe(2);
  expect(await cf('ip_panel_debug', 0), 'and not to the panel path').toBe(panelBefore + 1);
  expect(recordedNotes.length, 'the recorded presses sound on CANOPY').toBeGreaterThanOrEqual(2);
  await key('STOP');
  await expect.poll(async () => (await voices()).filter((v) => v.gate === 1).length, { timeout: 30_000 }).toBe(0);

  // Playback without recording plays both recorded notes on CANOPY.
  await key('PLAY');
  const played = await watch('playback', 6_000);
  await key('STOP');
  shot('record-playback');
  for (const n of recordedNotes) expect(near(played, n), `recorded note ${n} plays back`).toBe(true);
  await expect.poll(async () => (await voices()).filter((v) => v.gate === 1).length, { timeout: 30_000 }).toBe(0);
});
