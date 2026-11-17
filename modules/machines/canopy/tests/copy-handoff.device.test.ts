import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { repoRoot } from '@digi/test-support';
import { withSession } from '@digi/regression/support.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol, prepareRuntimePlanListedOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { waitEmulated } from '@digi/regression/runtime/waits.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';

describeOn('dt2', 'CANOPY Sound copy on the DT2', () => {
  // CANOPY Sound persistence (finding 151): one track Sound copy and paste with
  // the stock panel. The paste must carry the settings at copy time. Passing
  // this test does not prove all copy paths.
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-copy-handoff-card-'));
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
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-copy-handoff-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

  test('copy an edited CANOPY Sound to another track with its settings',
    { timeout: 900_000 }, async () => {
      const session = h.session;
      const artifacts = path.join(repoRoot(), 'out/canopy-copy-handoff', path.basename(session.runDir));
      fs.mkdirSync(artifacts, { recursive: true });
      const evidence: Record<string, unknown> = {};
      onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
      const { syx, plan } = await prepareRuntimePlanListedOffline(
        session.options, session.runDir, artifacts,
        ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_admission', 'ip_src_debug', 'src_state',
          'ip_sa_info', 'ip_sa_stats'],
        { modules: ['core/instrument-play', 'core/digikit-menu', 'core/health'] },
      );
      await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true,
        auto_no: true, card: session.cardPath });
      await session.client.call('run.mode', { mode: 'free' });
      const dk = liveProtocol(session);
      expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
      await dk.select(['modules/canopy.dkm']);
      const epoch = Number((await session.status()).epoch);
      await dk.restart();
      await dk.waitForBoot(undefined, 180_000);
      const word = async (address: number) => {
        const value = await session.client.memPeek(address);
        if (value === null) throw new Error(`Unreadable firmware word at ${address.toString(16)}`);
        return value >>> 0;
      };
      const cf = (name: string, index = 0) => word(planSymbol(plan, name) + 4 * index);
      await waitInstrumentReady(cf);
      await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, emulatedMs: 180_000 });
      const { tap: key, edge, turn } = acknowledgedPanel(session);
      await edge('TRK', true);
      try { await key('TRIG 1'); } finally { await edge('TRK', false); }
      const selectMachine = async (direction: 'UP' | 'DOWN') => {
        await edge('FUNC', true);
        try { await key('SRC'); } finally { await edge('FUNC', false); }
        for (let i = 0; i < 20; i++) await key(direction);
        await key('YES');
        await key('YES');
      };
      const instance = async () => {
        const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
        return tracks.indexOf(0);
      };
      const values = async () => {
        const index = await instance();
        if (index < 0) throw new Error('Track 1 is not admitted');
        const base = index * 48;
        const [metadata, first, strum] = await Promise.all([
          cf('src_state', base + 6), cf('src_state', base + 7), cf('src_state', base + 9),
        ]);
        return { mode: metadata & 3, chord: first >>> 16, voicing: first & 0xffff, strum: strum >>> 16 };
      };
      await selectMachine('DOWN');
      await expect.poll(instance, { timeout: 60_000 }).toBeGreaterThanOrEqual(0);
      await key('SRC');
      await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(0);
      for (let i = 0; i < 4 && (await cf('ip_src_debug', 1)) !== 0; i++) await key('SRC');
      await expect.poll(() => cf('ip_src_debug', 2), { timeout: 30_000 }).toBeGreaterThan(0);
      evidence.defaults = await values();
      await turn(2, 20);
      await turn(3, 12);
      await turn(4, 16);
      await expect.poll(values, { timeout: 30_000 }).toMatchObject({ mode: 1, voicing: 3 });
      evidence.edited = await values();
      expect(evidence.edited).not.toEqual(evidence.defaults);

      // DTII track copy and paste, with the TRIG held while RECORD/STOP is
      // pressed. The observed stock copy goes through memcpy of 0x450 bytes.
      const copies = async () => cf('ip_sa_stats', 0); // SA_STAT_COPIES
      const copiesBefore = await copies();
      await edge('TRK', true);
      await edge('TRIG 1', true);
      try { await key('RECORD'); } finally {
        await edge('TRIG 1', false);
        await edge('TRK', false);
      }
      await expect.poll(copies, { timeout: 30_000 }).toBeGreaterThan(copiesBefore);
      evidence.copyCount = await copies();
      // Change the source after copying; destination must receive the snapshot,
      // not the later live value or a pointer to mutable source state.
      await turn(2, 4);
      evidence.afterCopyEdit = await values();
      expect(evidence.afterCopyEdit).not.toEqual(evidence.edited);
      await edge('TRK', true);
      await edge('TRIG 2', true);
      try { await key('STOP'); } finally {
        await edge('TRIG 2', false);
        await edge('TRK', false);
      }
      await edge('TRK', true);
      try { await key('TRIG 2'); } finally { await edge('TRK', false); }
      await waitEmulated(session, 1000);
      const targetInstance = async () => {
        const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
        return tracks.indexOf(1);
      };
      await expect.poll(targetInstance, { timeout: 60_000 }).toBeGreaterThanOrEqual(0);
      const targetValues = async () => {
        const i = await targetInstance();
        if (i < 0) throw new Error('Track 2 is not admitted');
        const [metadata, first, strum] = await Promise.all([
          cf('src_state', i * 48 + 6), cf('src_state', i * 48 + 7), cf('src_state', i * 48 + 9),
        ]);
        return { mode: metadata & 3, chord: first >>> 16, voicing: first & 0xffff, strum: strum >>> 16 };
      };
      await expect.poll(targetValues, { timeout: 30_000 }).toEqual(evidence.edited);
      evidence.pasted = await targetValues();
      evidence.persistence = {
        info: await Promise.all(Array.from({ length: 8 }, (_, i) => cf('ip_sa_info', i))),
        stats: await Promise.all(Array.from({ length: 9 }, (_, i) => cf('ip_sa_stats', i))),
      };
      await session.expectNoFault('CANOPY Sound copy handoff');
    });
});
