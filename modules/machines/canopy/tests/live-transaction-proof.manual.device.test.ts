import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { repoRoot } from '@digi/test-support';
import { withSession } from '@digi/regression/support.ts';
import { liveOptions } from '@digi/regression/runtime/devices.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { seedModules } from '@digi/regression/runtime/card-seed.ts';

// Opt-in disposable emulator only. Public mapper batches are injected by the
// fixture; their outputs are checked without claiming physical MIDI/audio.
vi.setConfig({ hookTimeout: 600_000 });
if (process.env.DIGI_CANOPY_LIVE_PROOF === '1') {
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-live-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
    await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-live-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });
  test('public encoder and mapped MIDI publish immutable per-track snapshots after worker refill',
    { timeout: 900_000 }, async () => {
      const session = h.session;
      const artifacts = path.join(repoRoot(), 'out/canopy-live-proof', path.basename(session.runDir));
      fs.mkdirSync(artifacts, { recursive: true });
      const evidence: Record<string, unknown> = {};
      onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
      const { syx, plan } = await prepareRuntimePlanModulesOffline(session.options, session.runDir, artifacts,
        ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_admission', 'ip_src_debug', 'src_state',
          'canopy_sound_proof', 'canopy_memory_probe_entry', 'canopy_live_tick', 'canopy_live_test'],
        { modules: ['test/instrument-play-proof', 'core/digikit-menu', 'core/health',
          'test/canopy-sound-proof', 'test/canopy-live-proof'] });
      await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true,
        auto_no: true, card: session.cardPath });
      await session.client.call('run.mode', { mode: 'free' });
      const dk = liveProtocol(session);
      expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
      await dk.select(['modules/canopy.dkm']);
      const epoch = Number((await session.status()).epoch);
      await dk.restart();
      await dk.waitForBoot();
      const word = async (name: string, index = 0) => {
        const v = await session.client.memPeek(planSymbol(plan, name) + 4 * index);
        if (v === null) throw new Error(`Unreadable ${name}[${index}]`);
        return v >>> 0;
      };
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
      const readiness = async (track: number) => {
        const i = await instance(track);
        if (i < 0) return { track, instance: i, ready: false };
        const [admission, sound, generation, type, activeLoads, metadata] = await Promise.all([
          word('ip_admission', i * 8), word('ip_admission', i * 8 + 1),
          word('ip_admission', i * 8 + 4), word('ip_admission', i * 8 + 5),
          word('ip_admission', i * 8 + 6), word('src_state', i * 48 + 6),
        ]);
        return { track, instance: i, admission, sound, generation, type, activeLoads,
          mode: metadata & 3,
          ready: admission !== 0 && sound !== 0 && type !== 0 && activeLoads === 0 && (metadata & 3) === 1 };
      };
      try {
        for (let track = 0; track < 2; ++track)
          await expect.poll(() => readiness(track), { timeout: 60_000,
            message: `track ${track + 1} is admitted in ROOT mode` }).toMatchObject({ ready: true });
      } finally {
        evidence.beforeInitialization = await Promise.all([readiness(0), readiness(1)]);
      }
      await expect.poll(() => word('canopy_sound_proof'), { timeout: 60_000 }).toBeGreaterThan(0);
      const diagnostic = (index: number) => word('canopy_live_test', index);
      const poke = (index: number, value: number) => session.client.call('mem.cf.poke', {
        addr: planSymbol(plan, 'canopy_live_test') + 4 * index, value,
      });
      const state = () => Promise.all(Array.from({ length: 56 }, (_, i) => diagnostic(i)));
      const commands: { command: number; track: number; diagnostics: number[] }[] = [];
      evidence.commands = commands;
      const command = async (value: number, track = 0) => {
        const before = await diagnostic(3);
        await poke(1, track); await poke(0, value);
        try {
          await expect.poll(() => diagnostic(3), { timeout: 60_000 }).toBe(before + 1);
          const result = await diagnostic(2);
          expect(result, `command ${value}: initialization reason ${await diagnostic(38)}, track ${await diagnostic(39)}`)
            .toBe(0);
        } finally {
          commands.push({ command: value, track, diagnostics: await state() });
        }
      };
      const notes = async () => {
        const on = await diagnostic(32);
        expect(on).toBeGreaterThan(0);
        expect(await diagnostic(33)).toBe(on);
        expect(await diagnostic(34)).toBe(on);
        expect(await word('ip_src_debug', 0), 'MIDI track does not take UI focus').toBe(0);
      };
      await session.client.call('mem.cf.poke', {
        addr: planSymbol(plan, 'canopy_memory_probe_entry'), value: planSymbol(plan, 'canopy_live_tick'),
      });
      await poke(36, 1); await command(1);
      await expect.poll(() => diagnostic(6), { timeout: 120_000 }).toBe(1);
      evidence.failedInitialAllocation = await state();
      expect(await diagnostic(7)).toBe(0xffffffff);
      expect(await diagnostic(15)).toBe(0);
      expect(await diagnostic(4)).toBe(0);
      await poke(36, 0); await command(2);
      await expect.poll(() => diagnostic(4), { timeout: 120_000 }).toBe(1);
      evidence.baseline = await state();
      for (const base of [40, 48]) {
        expect(await diagnostic(base + 7), 'fixture credits retain zero as unbound')
          .toBe((await diagnostic(base + 2)) + 1);
      }
      expect(await diagnostic(35)).toBe(2);
      expect(await diagnostic(22)).toBe(1);
      expect(await diagnostic(30)).toBe(2);
      const initial0 = await diagnostic(19), initial1 = await diagnostic(27);
      await turn(4, 1); // Initial snapshots consumed both reservations.
      await expect.poll(() => diagnostic(18), { timeout: 30_000 }).toBeGreaterThan(0);
      expect(await diagnostic(22)).toBe(1);
      expect(await diagnostic(19)).toBe(initial0);
      await command(2);
      await expect.poll(() => diagnostic(6), { timeout: 120_000 }).toBe(3);
      expect(await diagnostic(7)).toBe(0);
      await turn(4, 1);
      await expect.poll(() => diagnostic(17), { timeout: 30_000 }).toBe(1);
      expect(await diagnostic(22)).toBe(2);
      expect(await diagnostic(19)).not.toBe(initial0);
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
      await expect.poll(() => diagnostic(6), { timeout: 120_000 }).toBe(4);
      await command(3, 1); await notes();
      expect(await diagnostic(25)).toBe(2);
      expect(await diagnostic(29)).toBe(3);
      await turn(4, 1);
      await expect.poll(() => diagnostic(17), { timeout: 30_000 }).toBe(2);
      await expect.poll(() => diagnostic(35), { timeout: 30_000 }).toBe(6);
      const final = await state(); evidence.final = final;
      expect(final[22]).toBe(3); expect(final[30]).toBe(2);
      expect(final[23]).toBe(1); expect(final[31]).toBe(1);
      expect(final[14]).toBe(0); expect(final[10]).toBe(0);
      expect((final[9]! | final[11]!) & 0x700).toBe(0);
      expect(final[12]).toBeGreaterThan(0); expect(final[13]).toBeGreaterThan(0);
      expect(final[12]).not.toBe(final[13]);
      await command(4); expect(await diagnostic(4)).toBe(0);
      await session.expectNoFault('public CANOPY immutable snapshot transactions');
    });
} else {
  test.skip('CANOPY live transaction proof is opt-in', () => {});
}
