import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { repoRoot, Session } from '@digi/test-support';
import { withSession } from '@digi/regression/support.ts';
import { liveOptions } from '@digi/regression/runtime/devices.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { waitEmulated } from '@digi/regression/runtime/waits.ts';
import { cardAfter } from '@digi/regression/runtime/write-safety.ts';
import { currentRecord, recordsIn } from '@digi/regression/runtime/card-kit.ts';

// Opt-in disposable emulator only. Public mapper batches use the same pool as
// the deserializer; this does not claim complete Sound ownership or hardware safety.
vi.setConfig({ hookTimeout: 600_000 });
if (process.env.DIGI_CANOPY_INTEGRATED_PROOF === '1') {
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-live-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(() => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
    const modules = path.join(cardSeed, 'digikit', 'modules');
    fs.mkdirSync(modules, { recursive: true });
    fs.copyFileSync(moduleFile, path.join(modules, 'canopy.dkm'));
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-live-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });
  test('live edits attach to the shared Sound pool without stale restore',
    { timeout: 900_000 }, async () => {
      const session = h.session;
      const artifacts = path.join(repoRoot(), 'out/canopy-integrated-proof', path.basename(session.runDir));
      fs.mkdirSync(artifacts, { recursive: true });
      const evidence: Record<string, unknown> = {};
      const { syx, plan } = await prepareRuntimePlanModulesOffline(session.options, session.runDir, artifacts,
        ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_admission', 'ip_src_debug', 'src_state',
          'canopy_sound_proof', 'canopy_memory_probe_entry', 'canopy_integrated_tick', 'canopy_integrated_test'],
        { modules: ['test/instrument-play-proof', 'core/digikit-menu', 'core/health',
          'test/canopy-sound-proof', 'test/canopy-integrated-proof'] });
      await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true,
        auto_no: true, card: session.cardPath });
      await session.client.call('run.mode', { mode: 'free' });
      const dk = liveProtocol(session);
      expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
      await dk.select(['modules/canopy.dkm']);
      const epoch = Number((await session.status()).epoch);
      await dk.restart();
      await dk.waitForBoot(undefined, 180_000);
      const word = async (name: string, index = 0) => {
        const v = await session.client.memPeek(planSymbol(plan, name) + 4 * index);
        if (v === null) throw new Error(`Unreadable ${name}[${index}]`);
        return v >>> 0;
      };
      onTestFinished(async () => {
        // Collect bounded post-failure evidence while the session is still
        // available, including CF state even if the DSP has stopped advancing.
        const bounded = async (name: string, fn: () => Promise<unknown>) => {
          try {
            evidence[name] = await Promise.race([
              fn(),
              new Promise<never>((_, reject) => setTimeout(() => reject(new Error('diagnostic timeout')), 5000)),
            ]);
          } catch (error) { evidence[name] = String(error); }
        };
        await bounded('finalStatus', () => session.status());
        await bounded('finalSoundProof', () => Promise.all(Array.from({ length: 8 }, (_, i) => word('canopy_sound_proof', i))));
        await bounded('finalBridge', () => Promise.all(Array.from({ length: 38 }, (_, i) => word('canopy_integrated_test', i))));
        fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2));
      });
      await waitInstrumentReady(word);
      await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, emulatedMs: 180_000 });
      const { tap, edge, turn } = acknowledgedPanel(session);
      const selectTrack = async (track: number) => {
        await edge('TRK', true);
        try { await tap(`TRIG ${track + 1}`); } finally { await edge('TRK', false); }
      };
      const instance = async (track: number) =>
        (await Promise.all(Array.from({ length: 4 }, (_, i) => word('ip_tracks', i)))).indexOf(track);
      const keysPage = async (track: number) => {
        await selectTrack(track);
        await tap('SRC');
        for (let i = 0; i < 4 && (await word('ip_src_debug', 1)) !== 0; ++i) await tap('SRC');
        await expect.poll(() => word('ip_src_debug', 0), { timeout: 30_000 }).toBe(track);
        await expect.poll(() => word('ip_src_debug', 2), { timeout: 30_000 }).toBeGreaterThan(0);
      };
      for (let track = 0; track < 2; ++track) {
        await selectTrack(track);
        await edge('FUNC', true);
        try { await tap('SRC'); } finally { await edge('FUNC', false); }
        for (let i = 0; i < 20; ++i) await tap('DOWN');
        await tap('YES'); await tap('YES');
        await expect.poll(() => instance(track), { timeout: 60_000 }).toBeGreaterThanOrEqual(0);
        await keysPage(track);
        // PLAY -> ROOT MAJ / ROOT MIN, with distinct initial STRM values.
        await turn(2, 4 * (track + 1));
        await turn(4, track + 1);
        await expect.poll(async () => word('src_state', (await instance(track)) * 48 + 6),
          { timeout: 30_000 }).toSatisfy((metadata: number) => (metadata & 3) === 1);
      }
      await keysPage(0);
      await expect.poll(() => word('canopy_sound_proof'), { timeout: 60_000 }).toBeGreaterThan(0);
      const diagnostic = (index: number) => word('canopy_integrated_test', index);
      const poke = (index: number, value: number) => session.client.call('mem.cf.poke', {
        addr: planSymbol(plan, 'canopy_integrated_test') + 4 * index, value,
      });
      const command = async (value: number, track = 0) => {
        const before = await diagnostic(3);
        await poke(1, track); await poke(0, value);
        await expect.poll(() => diagnostic(3), { timeout: 60_000 }).toBe(before + 1);
        expect(await diagnostic(2)).toBe(0);
      };
      const state = () => Promise.all(Array.from({ length: 38 }, (_, i) => diagnostic(i)));
      const notes = async () => {
        const on = await diagnostic(32);
        expect(on).toBeGreaterThan(0);
        expect(await diagnostic(33)).toBe(on);
        expect(await diagnostic(34)).toBe(on);
        expect(await word('ip_src_debug', 0), 'MIDI track does not take UI focus').toBe(0);
      };
      await session.client.call('mem.cf.poke', {
        addr: planSymbol(plan, 'canopy_memory_probe_entry'), value: planSymbol(plan, 'canopy_integrated_tick'),
      });
      await command(1);
      await expect.poll(() => diagnostic(6), { timeout: 120_000 }).toBe(1);
      await expect.poll(() => diagnostic(4), { timeout: 120_000 }).toBe(1);
      evidence.baseline = await state();
      expect(await diagnostic(35)).toBe(2);
      expect(await diagnostic(22)).toBe(1);
      expect(await diagnostic(30)).toBe(2);
      const initial0 = await diagnostic(19), initial1 = await diagnostic(27);
      await turn(4, 1); // Initial snapshots consumed both reservations.
      await expect.poll(() => diagnostic(18), { timeout: 30_000 }).toBeGreaterThan(0);
      expect(await diagnostic(22)).toBe(1);
      expect(await diagnostic(19)).toBe(initial0);
      await command(2);
      await expect.poll(() => diagnostic(6), { timeout: 120_000 }).toBe(2);
      expect(await diagnostic(7)).toBe(0);
      const restores = await word('canopy_sound_proof', 2);
      await turn(4, 1);
      await expect.poll(() => diagnostic(17), { timeout: 30_000 }).toBe(1);
      expect(await diagnostic(22)).toBe(2);
      expect(await diagnostic(19)).not.toBe(initial0);
      await waitEmulated(session, 500);
      expect(await word('canopy_sound_proof', 2), 'own edit does not trigger tick restore').toBe(restores);
      await command(5); // an older immutable handle must not restore over this edit
      expect(await diagnostic(22), 'stale restore leaves the edit intact').toBe(2);
      const refused = await diagnostic(18);
      await turn(4, 1);
      await expect.poll(() => diagnostic(18), { timeout: 30_000 }).toBeGreaterThan(refused);
      expect(await diagnostic(22)).toBe(2);
      await command(3, 1); await notes();
      expect(await diagnostic(25)).toBe(1);
      expect(await diagnostic(29)).toBe(2);
      expect(await diagnostic(27)).not.toBe(initial1);
      expect(await diagnostic(21)).toBe(0);
      await command(3, 1); await notes(); // Selector refused; notes still release.
      expect(await diagnostic(26)).toBe(1);
      expect(await diagnostic(29)).toBe(2);
      expect(await diagnostic(25)).toBe(1);
      evidence.exhausted = await state();
      await command(2);
      await expect.poll(() => diagnostic(6), { timeout: 120_000 }).toBe(3);
      await command(3, 1); await notes();
      expect(await diagnostic(25)).toBe(2);
      expect(await diagnostic(29)).toBe(3);
      await turn(4, 1);
      await expect.poll(() => diagnostic(17), { timeout: 30_000 }).toBe(2);
      await expect.poll(() => diagnostic(35), { timeout: 30_000 }).toBe(6);
      const final = await state(); evidence.final = final;
      expect(final[22]).toBe(3); expect(final[30]).toBe(2);
      expect(final[23]).toBe(1); expect(final[31]).toBe(1);
      expect(final[14]).toBe(0);
      expect(final[11]! & 0x700).toBe(0);
      expect(final[12]).toBeGreaterThan(0); expect(final[13]).toBeGreaterThan(0);
      expect(final[12]).not.toBe(final[13]);
      await command(4); expect(await diagnostic(4)).toBe(0);
      // Stop the test-only live callbacks before exercising the ordinary
      // project writer and reader. Both proof paths use the same SpPool.
      await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'canopy_memory_probe_entry'), value: 0 });
      const savedBefore = await word('canopy_sound_proof', 4);
      await edge('FUNC', true);
      try { await tap('SETTINGS'); } finally { await edge('FUNC', false); }
      await waitEmulated(session, 300); await tap('YES'); await waitEmulated(session, 3500);
      expect(await word('canopy_sound_proof', 4), 'stock writer captured CANOPY settings').toBeGreaterThan(savedBefore);
      const loadedBefore = await word('canopy_sound_proof', 1);
      evidence.reloadStage = 'open settings';
      await tap('SETTINGS');
      // Project recall can stop DSP frames while the CF UI and loader keep
      // running. Panel acknowledgement follows the CF input queue instead of
      // assuming the DSP is a clock throughout this transition.
      for (let i = 0; i < 3; ++i) {
        evidence.reloadStage = `confirm ${i + 1}`;
        await tap('YES');
      }
      evidence.reloadStage = 'final confirmation';
      await tap('YES');
      evidence.reloadStage = 'wait for Sound load';
      await expect.poll(() => word('canopy_sound_proof', 1), { timeout: 120_000,
        message: 'stock loader published into the shared pool' }).toBeGreaterThan(loadedBefore);
      await expect.poll(async () => {
        const a = await instance(0), b = await instance(1);
        if (a < 0 || b < 0) return false;
        const first = await word('src_state', a * 48 + 9);
        const second = await word('src_state', b * 48 + 7);
        return (first >>> 16) === 3 && (second >>> 16) === final[29];
      }, { timeout: 60_000, message: 'loaded Sound values survive the project roundtrip' }).toBe(true);
      evidence.reloadStage = 'complete';
      await session.expectNoFault('public CANOPY immutable snapshot transactions');

      // Materialize only this disposable card's writes and boot a separate
      // emulator process on it. No pool, handles, or fixture globals can be
      // inherited from the original daemon.
      const overlay = path.join(artifacts, 'saved-card.overlay.bin');
      await session.client.call('card.overlay', { path: overlay });
      if (!session.cardPath) throw new Error('No card available for the cold boot');
      const savedCard = cardAfter(session.cardPath, overlay, path.join(session.runDir, 'integrated-saved-card'));
      evidence.coldSelection = currentRecord(recordsIn(savedCard, 'dt2'));
      expect((evidence.coldSelection as { paths?: string[] } | null)?.paths).toEqual(['modules/canopy.dkm']);
      const coldSession = await Session.start({ ...options, card: undefined, dsp: 'sharc', env: { DIGI_COLD_RELAX_FS: '1' } });
      const coldWord = async (name: string, index = 0) => {
        const v = await coldSession.client.memPeek(planSymbol(plan, name) + 4 * index);
        if (v === null) throw new Error(`Unreadable cold ${name}[${index}]`);
        return v >>> 0;
      };
      onTestFinished(async () => {
        try {
          evidence.coldFinalStatus = await Promise.race([
            coldSession.status(),
            new Promise<never>((_, reject) => setTimeout(() => reject(new Error('cold status timeout')), 5000)),
          ]);
          evidence.coldFinalProof = await Promise.race([
            Promise.all(Array.from({ length: 8 }, (_, i) => coldWord('canopy_sound_proof', i))),
            new Promise<never>((_, reject) => setTimeout(() => reject(new Error('cold proof timeout')), 5000)),
          ]);
        } catch (error) { evidence.coldDiagnosticError = String(error); }
        fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2));
        await coldSession.close();
      });
      const coldEpoch = Number((await coldSession.status()).epoch);
      evidence.coldStage = 'firmware load';
      await coldSession.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true,
        auto_no: true, card: savedCard });
      await coldSession.client.call('run.mode', { mode: 'free' });
      await waitInstrumentReady(coldWord);
      await expectRuntimeUi(coldSession, plan, artifacts, { afterEpoch: coldEpoch, emulatedMs: 180_000 });
      evidence.coldModules = await liveProtocol(coldSession).modules();
      expect((evidence.coldModules as { name: string }[]).map((m) => m.name)).toContain('digikit/canopy');
      expect(await coldWord('canopy_integrated_test', 3), 'new emulator starts with no live-transaction commands').toBe(0);
      evidence.coldBeforeLoad = await Promise.all(Array.from({ length: 8 }, (_, i) => coldWord('canopy_sound_proof', i)));
      const coldPanel = acknowledgedPanel(coldSession);
      evidence.coldStage = 'open saved project';
      await coldPanel.tap('SETTINGS');
      for (let i = 0; i < 3; ++i) {
        evidence.coldStage = `confirm ${i + 1}`;
        await coldPanel.tap('YES');
      }
      evidence.coldStage = 'final confirmation';
      await coldPanel.tap('YES');
      evidence.coldStage = 'wait for Sound load';
      await expect.poll(() => coldWord('canopy_sound_proof', 1), { timeout: 120_000,
        message: 'cold project reload publishes saved Sounds' }).toBeGreaterThan((evidence.coldBeforeLoad as number[])[1]!);
      await expect.poll(async () => {
        const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => coldWord('ip_tracks', i)));
        const a = tracks.indexOf(0), b = tracks.indexOf(1);
        if (a < 0 || b < 0) return false;
        const first = await coldWord('src_state', a * 48 + 9);
        const second = await coldWord('src_state', b * 48 + 7);
        evidence.coldValues = { firstStrum: first >>> 16, secondChord: second >>> 16 };
        return (first >>> 16) === 3 && (second >>> 16) === final[29];
      }, { timeout: 60_000, message: 'both edited SRC values survive a cold emulator boot' }).toBe(true);
      evidence.coldAfterLoad = await Promise.all(Array.from({ length: 8 }, (_, i) => coldWord('canopy_sound_proof', i)));
      evidence.coldStatus = await coldSession.status();
      evidence.coldStage = 'complete';
      await coldSession.expectNoFault('cold CANOPY project reload');
    });
} else {
  test.skip('CANOPY live transaction proof is opt-in', () => {});
}
