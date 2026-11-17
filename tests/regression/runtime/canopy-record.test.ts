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
import { waitEmulated } from './port-compat.ts';

// Recording keyboard presses on a CANOPY track (findings 147 and 125). While
// the sequencer live records, CANOPY hands KEYBOARD presses to the stock note
// action, which records them as trigs, and still plays them itself on the
// panel path (the stock audition is muted). Other keys held with a recorded
// press are stored on its step as chord-note locks, so playback sounds the
// whole chord on CANOPY. A held trig keeps CANOPY's SRC page while its knobs
// lock that step.
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
  const { tap: key, edge, turn } = acknowledgedPanel(session);
  const artifacts = path.join(root, 'out/canopy-record');
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

  // Grid recording: hold step 5 and lock a CANOPY knob. The stock edit mark
  // (sequencer byte +0x268) must not take the SRC page away from CANOPY.
  const ipDraw = planSymbol(plan, 'ip_src_draw');
  const manager = await cf('ip_src_refresh', 2);
  let node = await word(manager + 0x1c),
    view = 0;
  for (let n = 0; n < 32 && node && node !== manager + 0x14; n++, node = await word(node)) {
    const v = await word(node + 8);
    if (v && (await word((await word(v)) + 16)) >>> 0 === ipDraw) {
      view = v;
      break;
    }
  }
  expect(view, 'visible SRC view').not.toBe(0);
  const seq = await word(view + 0x6c);
  const byteAt = async (a: number) => ((await word((a & ~3) >>> 0)) >>> ((3 - (a & 3)) * 8)) & 0xff;
  await key('RECORD');
  await waitEmulated(session, 1_000);
  shot('record-grid');
  expect(await byteAt(seq + 0x269), 'grid recording').toBe(1);
  // SOUND page (COLOR on knob B in the four-page layout), as in canopy-lock.
  for (let i = 0; i < 5 && (await cf('ip_src_debug', 1)) !== 2; i++) await key('SRC');
  expect(await cf('ip_src_debug', 1)).toBe(2);
  const heldMask = async () => (await word(seq + 0x258)) >>> 0;
  const panelBeforeHold = await cf('ip_panel_debug', 0);
  await edge('TRIG 5', true);
  try {
    await waitEmulated(session, 300);
    log.hold = {
      flags: [0x268, 0x269, 0x271, 0x272].map((o) => o.toString(16)),
      values: await Promise.all([0x268, 0x269, 0x271, 0x272].map((o) => byteAt(seq + o))),
      mask: await Promise.all([0, 4, 8, 12].map(async (o) => (await word(seq + 0x258 + o)) >>> 0)),
      panel: (await cf('ip_panel_debug', 0)) - panelBeforeHold,
      seq: seq.toString(16),
    };
    console.info('hold', JSON.stringify(log.hold));
    shot('record-hold');
    expect(await heldMask(), 'step 5 held').not.toBe(0);
    await turn(2, 20);
    await waitEmulated(session, 500);
    const drawsBefore = await cf('ip_src_debug', 3);
    await waitEmulated(session, 300);
    const heldEdit = {
      edited: await byteAt(seq + 0x268),
      published: await cf('ip_src_debug', 2),
      draws: (await cf('ip_src_debug', 3)) - drawsBefore,
    };
    shot('record-held-edit');
    log.heldEdit = heldEdit;
    console.info('held edit', heldEdit);
    expect(heldEdit.edited, 'the turn marked the held trig as edited').toBe(1);
    expect(heldEdit.published, 'CANOPY still owns the SRC page').not.toBe(0);
    expect(heldEdit.draws, 'and keeps drawing it').toBeGreaterThan(0);
  } finally {
    await edge('TRIG 5', false);
  }
  // Leave grid recording as canopy-lock does, after the lock's toast.
  await waitEmulated(session, 2_000);
  await key('RECORD');
  await waitEmulated(session, 1_000);
  shot('record-grid-left');

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
  expect(await cf('ip_panel_debug', 0), 'and CANOPY still plays them').toBe(panelBefore + 3);
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

  // A held chord while live recording (C, E, G = TRIG 9, 11, 13): CANOPY
  // sounds all three together; the step keeps E/G's note plus two chord-note
  // locks, and playback sounds the whole chord on CANOPY.
  await edge('RECORD', true);
  try {
    await key('PLAY');
  } finally {
    await edge('RECORD', false);
  }
  await waitEmulated(session, 4_000); // the "Live Rec" message takes the first presses
  const capturesBefore = await cf('ip_panel_chord', 3);
  // Largest set of CANOPY notes gated at once, over `ms` of emulated time
  // (a 64-step pattern takes 8 s; the emulator can run slower than real time).
  const together = async (ms: number) => {
    let most: number[] = [],
      done = false;
    const elapsed = waitEmulated(session, ms).then(() => {
      done = true;
    });
    while (!done) {
      const gated = (await voices()).filter((v) => v.gate === 1 && v.hz > 0).map((v) => Math.round(semitone(v.hz)));
      if (gated.length > most.length) most = gated.sort((a, b) => a - b);
      await delay(30);
    }
    await elapsed;
    return most;
  };
  const chordKeys = ['TRIG 9', 'TRIG 11', 'TRIG 13'],
    held: string[] = [];
  let heldChord: number[] = [];
  try {
    for (const k of chordKeys) {
      await edge(k, true);
      held.push(k);
      await delay(60);
    }
    heldChord = await together(800);
  } finally {
    for (const k of held.reverse()) await edge(k, false);
  }
  await waitEmulated(session, 1_000); // the recorder commits after the key events
  const chordDiag = await Promise.all([0, 1, 2, 3, 4, 5, 6, 7].map((i) => cf('ip_panel_chord', i)));
  // Track 1's recorded steps (finding 149): trig word at +2*step, note at +0x280+step.
  const block = (await word(0x4316501c)) >>> 0;
  const trigByte = async (a: number) => ((await word((a & ~3) >>> 0)) >>> ((3 - (a & 3)) * 8)) & 0xff;
  const recordedSteps: string[] = [];
  for (let st = 0; st < 64; st++) {
    const w = ((await trigByte(block + 2 * st)) << 8) | (await trigByte(block + 2 * st + 1));
    if (w & 1) {
      const fields: number[] = [];
      for (const o of [0x100, 0x180, 0x200, 0x280, 0x300, 0x380, 0x400]) fields.push(await trigByte(block + o + st));
      recordedSteps.push(`${st}:${w.toString(16)}:${fields.join('/')}`);
    }
  }
  // Lock lanes (finding 147): 80 x 0x102 bytes at +0x17e50 {index, track, 128 x s16}.
  const lanes: string[] = [];
  for (let lane = 0; lane < 80; lane++) {
    const at = block + 0x17e50 + lane * 0x102;
    const index = await trigByte(at),
      laneTrack = await trigByte(at + 1);
    if (laneTrack !== 0 || index === 0xff) continue;
    const values: string[] = [];
    for (const entry of recordedSteps) {
      const st = Number(entry.split(':')[0]);
      const v = ((await trigByte(at + 2 + 2 * st)) << 8) | (await trigByte(at + 3 + 2 * st));
      if (v !== 0xffff) values.push(`${st}=${v}`);
    }
    if (values.length) lanes.push(`lane${lane}/idx${index}: ${values.join(' ')}`);
  }
  const stores: string[] = [];
  for (let i = 0; i < Math.min(16, chordDiag[3]!); i++) {
    const v = await cf('ip_panel_chord_log', i);
    stores.push(`step${v >>> 24}/note${(v >>> 16) & 255}/group${(v >>> 8) & 255}/n${v & 255}`);
  }
  log.chordRecording = { heldChord, chordDiag, block: block.toString(16), recordedSteps, lanes, stores };
  console.info('chord recording', JSON.stringify(log.chordRecording));
  expect(heldChord, 'all held keys sound together while recording').toEqual([60, 64, 67]);
  expect(chordDiag[3]! - capturesBefore, 'a committed step received the chord').toBeGreaterThanOrEqual(1);
  expect(chordDiag[1], 'the last stored chord holds the two other held keys').toBe(2);
  expect(chordDiag[2], 'chord-note locks written').toBe(1);
  await key('STOP');
  await expect.poll(async () => (await voices()).filter((v) => v.gate === 1).length, { timeout: 30_000 }).toBe(0);
  await key('PLAY');
  const playedChord = await together(9_000);
  await key('STOP');
  shot('record-chord-playback');
  log.chordPlayback = playedChord;
  console.info('chord playback', playedChord);
  for (const n of [60, 64, 67]) expect(playedChord, `chord note ${n} plays back with the others`).toContain(n);
  await expect.poll(async () => (await voices()).filter((v) => v.gate === 1).length, { timeout: 30_000 }).toBe(0);

  // Step recording (RECORD + STOP) enters notes with KEYBOARD held: the stock
  // key handler writes each key on the cursor step without the note action.
  // A held D, F, A (TRIG 10, 12, 14) sounds together on CANOPY and is stored
  // on its step, which plays back as that chord.
  await edge('RECORD', true);
  try {
    await key('STOP');
  } finally {
    await edge('RECORD', false);
  }
  await waitEmulated(session, 4_000); // the mode message takes the first presses
  shot('record-step-mode');
  const stepStoresBefore = await cf('ip_panel_chord', 3);
  const startedBefore = await cf('ip_panel_step', 1);
  const expiredBefore = await cf('ip_panel_step', 6);
  let stepChord: number[] = [];
  await edge('KEYBOARD', true);
  try {
    await waitEmulated(session, 200);
    const stepHeld: string[] = [];
    try {
      // Pressed back to back, as a hand plays a chord: the entries are
      // written after the key events, so presses arrive before earlier
      // entries appear.
      for (const k of ['TRIG 10', 'TRIG 12', 'TRIG 14']) {
        await edge(k, true);
        stepHeld.push(k);
      }
      stepChord = await together(600);
    } finally {
      for (const k of stepHeld.reverse()) await edge(k, false);
    }
  } finally {
    await edge('KEYBOARD', false);
  }
  await waitEmulated(session, 500);
  const stepDiag = await Promise.all([0, 1, 2, 3].map((i) => cf('ip_panel_chord', i)));
  const stepEvents = await Promise.all([0, 1, 2, 3, 4, 5, 6, 7].map((i) => cf('ip_panel_step', i)));
  log.stepRecording = { stepChord, stepDiag, stepEvents };
  console.info('step recording', JSON.stringify(log.stepRecording));
  expect(stepChord, 'held step-record keys sound together').toEqual([62, 65, 69]);
  expect(stepDiag[3]! - stepStoresBefore, 'each entry stored its chord').toBe(3);
  expect(stepEvents[1]! - startedBefore, 'releases do not start extra entries').toBe(3);
  expect(stepEvents[6]! - expiredBefore, 'all entries were matched to their commits').toBe(0);
  expect(stepDiag[1], 'the last entry holds the two other keys').toBe(2);
  expect(stepDiag[2], 'chord-note locks written').toBe(1);
  await key('RECORD'); // leave step recording
  await waitEmulated(session, 1_000);
  await key('PLAY');
  const stepPlayed: number[][] = [];
  {
    let done = false;
    const elapsed = waitEmulated(session, 9_000).then(() => {
      done = true;
    });
    while (!done) {
      const g = (await voices()).filter((v) => v.gate === 1 && v.hz > 0).map((v) => Math.round(semitone(v.hz)));
      if (g.length) stepPlayed.push(g.sort((a, b) => a - b));
      await delay(30);
    }
    await elapsed;
  }
  await key('STOP');
  log.stepPlayback = stepPlayed.filter((g) => g.length >= 3);
  console.info('step playback', JSON.stringify(log.stepPlayback));
  expect(
    stepPlayed.some((g) => [62, 65, 69].every((n) => g.includes(n))),
    'the step-recorded chord plays back',
  ).toBe(true);
  await expect.poll(async () => (await voices()).filter((v) => v.gate === 1).length, { timeout: 30_000 }).toBe(0);
});
