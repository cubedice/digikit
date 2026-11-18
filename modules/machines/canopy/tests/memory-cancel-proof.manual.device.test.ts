import fs from 'node:fs';
import path from 'node:path';
import { repoRoot } from '@digi/test-support';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
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

// Synthetic command, one-byte LZ4 payload, worker task: does not prove
// the real transport, command-task identity, complete rollback, or hardware.
vi.setConfig({ hookTimeout: 600_000 });
if (process.env.DIGI_CANOPY_MEMORY_CANCEL === '1') {
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-memory-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
    await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-memory-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });
  test('memory recall refusal takes the stock cancellation path and returns',
    { timeout: 600_000 }, async () => {
      const session = h.session;
      const artifacts = path.join(repoRoot(), 'out/canopy-memory-cancel', path.basename(session.runDir));
      fs.mkdirSync(artifacts, { recursive: true });
      const evidence: Record<string, unknown> = {};
      onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
      const { syx, plan } = await prepareRuntimePlanModulesOffline(
        session.options, session.runDir, artifacts,
        ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_admission', 'ip_src_debug', 'src_state', 'canopy_sound_proof', 'canopy_memory_probe_entry', 'canopy_memory_probe_tick', 'canopy_memory_test', 'canopy_memory_target', 'canopy_memory_cleanup_count', 'canopy_memory_cleanup_ptr', 'canopy_load_guard'],
        { modules: ['test/instrument-play-proof', 'core/digikit-menu', 'core/health', 'test/canopy-sound-proof', 'test/canopy-memory-cancel'] },
      );
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
        const value = await session.client.memPeek(planSymbol(plan, name) + 4 * index);
        if (value === null) throw new Error(`Unreadable ${name}[${index}]`);
        return value >>> 0;
      };
      await waitInstrumentReady(word);
      await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, emulatedMs: 180_000 });
      // Successful stock deserialization registers sound-proof's UI tick.
      await expect.poll(() => word('canopy_sound_proof', 0), { timeout: 60_000 }).toBeGreaterThan(0);
      // Give the refusal a real edited CANOPY Sound to preserve.
      const { tap: key, edge, turn } = acknowledgedPanel(session);
      await edge('TRK', true);
      try { await key('TRIG 1'); } finally { await edge('TRK', false); }
      await edge('FUNC', true);
      try { await key('SRC'); } finally { await edge('FUNC', false); }
      for (let i = 0; i < 20; i++) await key('DOWN');
      await key('YES');
      await key('YES');
      let index = -1;
      await expect.poll(async () => {
        const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => word('ip_tracks', i)));
        index = tracks.indexOf(0);
        return index;
      }, { timeout: 60_000 }).toBeGreaterThanOrEqual(0);
      await key('SRC');
      await expect.poll(() => word('ip_src_debug', 0), { timeout: 30_000 }).toBe(0);
      for (let i = 0; i < 4 && (await word('ip_src_debug', 1)) !== 0; i++) await key('SRC');
      await expect.poll(() => word('ip_src_debug', 1), { timeout: 30_000 }).toBe(0);
      await expect.poll(() => word('ip_src_debug', 2), { timeout: 30_000 }).toBeGreaterThan(0);
      await turn(2, 20);
      await turn(3, 12);
      await expect.poll(async () => ({ mode: (await word('src_state', index * 48 + 6)) & 3,
        voicing: (await word('src_state', index * 48 + 7)) & 0xffff }), { timeout: 30_000 })
        .toEqual({ mode: 1, voicing: 3 });
      const selected = [await word('src_state', index * 48 + 6), await word('src_state', index * 48 + 7), await word('src_state', index * 48 + 9)];
      const sound = await word('ip_admission', index * 8 + 1);
      expect(sound, 'admitted Sound pointer').not.toBe(0);
      await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'canopy_memory_target'), value: sound });
      await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'canopy_memory_probe_entry'), value: planSymbol(plan, 'canopy_memory_probe_tick') });
      await session.client.call('mem.cf.poke', { addr: planSymbol(plan, 'canopy_memory_test'), value: 1 });
      await expect.poll(() => word('canopy_memory_test', 11), { timeout: 180_000 }).toBe(1);
      evidence.test = await Promise.all(Array.from({ length: 14 }, (_, i) => word('canopy_memory_test', i)));
      evidence.guard = await Promise.all(Array.from({ length: 11 }, (_, i) => word('canopy_load_guard', i)));
      evidence.cleanup = [await word('canopy_memory_cleanup_count'), await word('canopy_memory_cleanup_ptr')];
      const result = evidence.test as number[];
      expect(result[1], 'background job accepted').toBeGreaterThan(0);
      expect(result[2], 'synthetic dispatcher returned').toBe(3);
      expect(result[4], 'stock start-recall transition').toBe(11);
      expect(result[7], 'stock cancel-recall transition').toBe(1);
      expect(result[8]).toBe(result[5]! + 1);
      expect(result[9]).toBe(result[6]! + 1);
      expect(result[10], 'background job finished successfully').toBe(0);
      expect(result[12], 'full Sound hash before refusal').not.toBe(0);
      expect(result[13], 'full Sound hash after refusal').toBe(result[12]);
      expect(evidence.cleanup, 'temporary buffer reached the stock cleanup path').toEqual([1, expect.any(Number)]);
      expect((evidence.cleanup as number[])[1], 'temporary buffer was allocated').not.toBe(0);
      expect(await word('ip_admission', index * 8 + 1), 'same admitted Sound').toBe(sound);
      expect([await word('src_state', index * 48 + 6), await word('src_state', index * 48 + 7), await word('src_state', index * 48 + 9)], 'edited SRC values remain intact').toEqual(selected);
      await session.expectNoFault('synthetic memory recall cancellation');
    });
} else {
  test.skip('CANOPY synthetic memory recall experiment is opt-in', () => {});
}
