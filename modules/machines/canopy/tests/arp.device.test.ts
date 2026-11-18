import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { withSession } from '@digi/regression/support.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol, prepareRuntimePlanListedOffline, type RuntimePlan } from '@digi/regression/runtime/runtime-plan.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { canopyControls } from '@digi/regression/runtime/canopy-controls.ts';
import { encoderValue } from '@digi/regression/runtime/encoder-value.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { REDRAW, SETTLE } from '@digi/regression/runtime/fx-live.ts';
import { openDigikitMenu } from '@digi/regression/runtime/flows/menu.ts';
import { seedModules } from '@digi/regression/runtime/card-seed.ts';

describeOn('dt2', 'CANOPY arp on the DT2', () => {
  // Full DTII 1.17 firmware, real panel input and MIDI, with a disposable card.
  // Only stock tempo/receive settings are fixture inputs: no fabricated timing
  // provider, instrument configuration or DSP state is written.
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-arp-card-'));
  const moduleFile = process.env.CANOPY_ARP_PACKAGE
    ? path.resolve(repoRoot(), process.env.CANOPY_ARP_PACKAGE)
    : moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
    await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-arp-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

  test('CANOPY arp follows BPM across stopped and playing transport', { timeout: 900_000 }, async () => {
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
      : await prepareRuntimePlanListedOffline(
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
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: beforeRestart, emulatedMs: 180_000 });
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
    // A held, manually placed step must preview its locked chord on KEYS,
    // using the track's inherited NOTE rather than an invented default pitch.
    await key(1); // TRIG parameter page
    await sessionRt(session).ui.wait({ ms: 3000, why: SETTLE }); // let the page-change message clear
    const pattern = await word(0x4316501c);
    const byteAt = async (address: number) => ((await word(address & ~3)) >>> ((3 - (address & 3)) * 8)) & 0xff;
    const originalRoot = await byteAt(pattern + 0x485);
    for (let i = 0; i < 3; i++) {
      await turn(1, -20);
      await sessionRt(session).ui.wait({ ms: 200, why: REDRAW });
    }
    const rootNote = await byteAt(pattern + 0x485);
    expect(rootNote, 'track NOTE changed').toBeLessThan(originalRoot);
    await key('SRC');
    await srcPage(0);
    await key('RECORD');
    await key('TRIG 1');
    await edge('TRIG 1', true);
    try {
      const display = planSymbol(plan, 'display');
      await turn(2, 20); // CHRD
      await expect.poll(() => byteAt(display + 201), { timeout: 30_000 }).toBe(2);
      await expect
        .poll(async () => Promise.all(Array.from({ length: 3 }, (_, i) => byteAt(display + 270 + i))), {
          timeout: 30_000,
        })
        .toEqual([rootNote, rootNote + 3, rootNote + 7]);
    } finally {
      await edge('TRIG 1', false);
    }
    // Read the native trig flag instead of assuming another press removes it:
    // holding/editing a trig has its own stock toggle handling.
    const stepOneSet = async () => (await byteAt(pattern + 1)) & 1;
    log.previewStepBeforeCleanup = await stepOneSet();
    if (await stepOneSet()) await key('TRIG 1');
    await expect.poll(stepOneSet, { timeout: 30_000 }).toBe(0);
    const activeSteps = async () => {
      const trigPairs = await Promise.all(Array.from({ length: 64 }, (_, i) => word(pattern + 4 * i)));
      return trigPairs.flatMap((pair, i) => [
        ...((pair & 0x10000) !== 0 ? [2 * i] : []),
        ...((pair & 1) !== 0 ? [2 * i + 1] : []),
      ]);
    };
    // Stock initialization can seed factory trigs even on a disposable card.
    // Clear this test track with the native grid-record clear-track shortcut;
    // otherwise PLAY would correctly add sequenced notes to the arp's chord.
    log.timingFixtureStepsBeforeClear = await activeSteps();
    await edge('FUNC', true);
    try {
      await key('PLAY');
    } finally {
      await edge('FUNC', false);
    }
    await expect
      .poll(activeSteps, { timeout: 30_000, message: 'timing fixture has no sequenced notes beside held MIDI' })
      .toEqual([]);
    log.timingFixtureActiveSteps = await activeSteps();
    await key('RECORD');
    await key(1); // restore the native track NOTE changed only for the preview
    // Stock NOTE accumulates encoder fractions before moving one semitone.
    // Single-detent acknowledged turns allow filtered/sub-semitone packets;
    // larger packets must not be interpreted as one semitone per detent.
    await encoderValue(
      (delta) => turn(1, delta),
      () => byteAt(pattern + 0x485),
      originalRoot,
      { maxTurns: 128 },
    );
    expect(await byteAt(pattern + 0x485), 'native track NOTE restored').toBe(originalRoot);
    // Return from TRIG explicitly. SRC diagnostics retain their last page while
    // another native page is visible, so they cannot choose this first press.
    const drawsBeforeReturn = await cf('ip_src_debug', 3);
    await key('SRC');
    await expect.poll(() => cf('ip_src_debug', 3), { timeout: 30_000 }).toBeGreaterThan(drawsBeforeReturn);
    await srcPage(0);
    await openDigikitMenu({ ...sessionRt(session), dk });
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
      const instance = await peek(state, 188 * 4);
      const b = instance.subarray(136 * 4);
      return {
        token: b.readUInt32LE(0),
        phase: b.readUInt32LE(10 * 4),
        enabled: b.readUInt32LE(11 * 4),
        ordinal: b.readUInt32LE(12 * 4),
        serial: b.readUInt32LE(13 * 4),
        note: b.readUInt32LE(15 * 4),
        block: b.readUInt32LE(29 * 4),
        mode: b.readUInt32LE(26 * 4),
        status: b.readUInt32LE(34 * 4),
        tempo120: b.readUInt32LE(37 * 4),
        phrase: b.readUInt32LE(38 * 4),
        metadata: b.readUInt32LE(39 * 4),
        transport: b.readUInt32LE(48 * 4),
        pattern: b.readUInt32LE(49 * 4),
        generation: b.readUInt32LE(50 * 4),
        boundary: b.readUInt32LE(51 * 4),
        foreground: Array.from({ length: 4 }, (_, i) => ({
          token: instance.readUInt32LE(i * 32),
          gate: instance.readUInt32LE(i * 32 + 4),
        })),
        owners: Array.from({ length: 10 }, (_, i) => ({
          token: instance.readUInt32LE(292 + i * 8),
          note: (instance.readUInt32LE(296 + i * 8) >>> 8) & 127,
          route: instance.readUInt32LE(296 + i * 8) & 63,
        })).filter((owner) => owner.token !== 0),
      };
    };
    const inputFlags = async () => (await peek(planSymbol(plan, '_iv_input') + 48, 4)).readUInt32LE();
    type ArpState = Awaited<ReturnType<typeof arp>>;
    const cadenceObservations: unknown[] = [];
    log.cadenceObservations = cadenceObservations;
    const assertCadence = (first: ArpState, last: ArpState, bpm: number, label: string) => {
      // Remove the in-progress remainder to compare actual onset times even if
      // a slow host skips several events between polls. The sample-block stamp
      // updates on every callback, in both transport states, and shares one
      // instance snapshot with phase and serial.
      const increment = 2 * bpm * 120 * 4;
      const elapsed = last.block - first.block - (last.phase - first.phase) / increment;
      const count = last.serial - first.serial;
      const expected = (count * 90000) / (bpm * 4);
      const observation = { label, bpm, first, last, elapsed, expected };
      // Keep failing endpoints too: assigning the returned observation to log
      // after this helper cannot capture an assertion failure inside it.
      cadenceObservations.push(observation);
      expect(first.mode, `${label}: BPM clock active before observation`).toBe(4);
      expect(last.mode, `${label}: BPM clock remains active`).toBe(4);
      expect(last.status, `${label}: arp remains running`).toBe(2);
      expect(first.tempo120, `${label}: initial selected BPM`).toBe(bpm * 120);
      expect(last.tempo120, `${label}: final selected BPM`).toBe(bpm * 120);
      expect(count, `${label}: arp keeps producing events`).toBeGreaterThan(0);
      expect(Math.abs(elapsed - expected), `${label}: onset spacing`).toBeLessThanOrEqual(2);
      // Serial alone cannot distinguish advancing notes from a phrase that
      // repeatedly restarts at its root.
      expect(notes, `${label}: first note belongs to the held chord`).toContain(first.note);
      expect(notes[(notes.indexOf(first.note) + count) % notes.length], `${label}: note order`).toBe(last.note);
      return observation;
    };
    const afterEvents = async (first: ArpState, count: number) => {
      await expect
        .poll(async () => (await arp()).serial, { timeout: 90_000 })
        .toBeGreaterThanOrEqual(first.serial + count);
      return arp();
    };
    const originalTempo120 = await word(0x402b67fc);
    const originalBpm = originalTempo120 / 120;
    log.originalTempo120 = originalTempo120;
    expect(originalTempo120).toBeGreaterThanOrEqual(3600);
    expect(originalTempo120).toBeLessThanOrEqual(36000);
    const setTempo = async (bpm: number) => {
      // Change the native selected-tempo fixture, never provider/DSP state.
      // STOP can reselect the project's stored tempo, so its continuity proof
      // restores the captured native tempo before the transport transition.
      expect(await word(0x4096683c), 'tempo fixture uses the stock master source').toBe(1);
      expect((await word(0x40966838)) | 0, 'no stock tempo override is active').toBeLessThanOrEqual(0);
      expect(await word(0x402b6800), 'tempo fixture selects internal clock only').toBe(1);
      await stockWord(0x402b67fc, bpm * 120);
      await expect.poll(async () => (await inputFlags()) >>> 16, { timeout: 30_000 }).toBe(bpm * 120);
      await expect.poll(async () => (await arp()).tempo120, { timeout: 30_000 }).toBe(bpm * 120);
    };
    const measureTempo = async (bpm: number, playing: boolean) => {
      // 0x402b67fc is the stock current tempo, in BPM*120 (finding 135).
      await setTempo(bpm);
      expect((await inputFlags()) & 0x800, 'selected BPM is available independently of phase health').toBe(0x800);
      expect((await word(0x43164f94)) === 1, 'stock transport matches the measured state').toBe(playing);
      const initialSerial = (await arp()).serial;
      await expect.poll(async () => (await arp()).serial, { timeout: 60_000 }).toBeGreaterThan(initialSerial + 1);
      const first = await arp();
      const last = await afterEvents(first, 6);
      return assertCadence(first, last, bpm, `${playing ? 'playing' : 'stopped'} ${bpm} BPM`);
    };
    await setTempo(120);
    for (const note of notes) await midi([0x90, note, 100]);
    await expect.poll(async () => (await arp()).serial, { timeout: 60_000 }).toBeGreaterThan(2);
    log.stopped120 = await measureTempo(120, false);
    await srcPage(0);
    const stoppedKnobBefore = await arp();
    for (let i = 0; i < 10; ++i) await turn(8, i % 2 ? 4 : -4); // native SRC LEV
    log.stoppedKnob = assertCadence(
      stoppedKnobBefore,
      await afterEvents(stoppedKnobBefore, 3),
      120,
      'moving SRC level while stopped',
    );
    log.stopped180 = await measureTempo(180, false);
    console.info('CANOPY arp: stopped timing follows stock 120/180 BPM', log.stopped120, log.stopped180);
    expect(await cf('src_state', 3), 'tempo follows the pattern without editing the sound').toBe(revision);

    // Transport changes must leave a held phrase and its phase intact in both
    // TRIG and CONT. No pattern boundary or provider phase is fabricated here.
    await srcPage(1);
    for (const restart of [0, 2]) {
      await encoderValue(
        (delta) => turn(6, delta),
        () => settled(11),
        restart,
      );
      await expect.poll(async () => (await arp()).status, { timeout: 90_000 }).toBe(2);
      await setTempo(180);
      // Start from an onset produced with the settled RST setting. The entire
      // subsequent PLAY interval remains subject to the cadence/order checks.
      const beforePlay = await afterEvents(await arp(), 1);
      await key('PLAY');
      await expect.poll(() => word(0x43164f94), { timeout: 30_000 }).toBe(1);
      log[`playTransition${restart}`] = assertCadence(
        beforePlay,
        await afterEvents(beforePlay, 6),
        180,
        `PLAY transition in restart mode ${restart}`,
      );
      log[`playing120Restart${restart}`] = await measureTempo(120, true);
      log[`playing180Restart${restart}`] = await measureTempo(180, true);
      await srcPage(0);
      const playingKnobBefore = await arp();
      for (let i = 0; i < 10; ++i) await turn(8, i % 2 ? 4 : -4);
      log[`playingKnobRestart${restart}`] = assertCadence(
        playingKnobBefore,
        await afterEvents(playingKnobBefore, 4),
        180,
        `moving SRC level while playing in restart mode ${restart}`,
      );
      await setTempo(originalBpm);
      const beforeStop = await afterEvents(await arp(), 1);
      await key('STOP');
      await expect.poll(() => word(0x43164f94), { timeout: 30_000 }).not.toBe(1);
      log[`stopTransition${restart}`] = assertCadence(
        beforeStop,
        await afterEvents(beforeStop, 6),
        originalBpm,
        `STOP transition in restart mode ${restart}`,
      );
      await srcPage(1);
    }
    console.info('CANOPY arp: exact BPM and note order persist across PLAY and STOP in TRIG/CONT');
    for (const note of notes) await midi([0x80, note, 0]);
    await expect.poll(async () => (await arp()).token, { timeout: 90_000 }).toBe(0);
    const released = (await arp()).serial;
    await sessionRt(session).ui.wait({ ms: 250, why: REDRAW });
    expect((await arp()).serial, 'released notes stop generating arp events').toBe(released);
    expect(await cf('ip_control')).toBe(3);
    expect(await cf('mt_guard_fault')).toBe(0);
    await session.expectNoFault('CANOPY pattern-tempo arp across stopped/running transport');
    const health = await session.status();
    log.health = health;
    expect(health.cf?.error, JSON.stringify(health)).toBeNull();
  });
});
