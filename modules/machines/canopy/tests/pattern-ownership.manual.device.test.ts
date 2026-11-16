import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { repoRoot } from '@digi/test-support';
import { withSession } from '@digi/regression/support.ts';
import { liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol, prepareRuntimePlanOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { waitEmulated } from '@digi/regression/runtime/waits.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';

// This failing-baseline regression is not registered for ordinary device runs.
// DIGI_CANOPY_PATTERN_PROOF selects unsafe emulator-only modules, never hardware.
vi.setConfig({ hookTimeout: 600_000 });
const enabled = process.env.DIGI_CANOPY_PATTERN_TEST === '1' || process.env.DIGI_CANOPY_PATTERN_PROOF === '1';
if (enabled) {
  const proof = process.env.DIGI_CANOPY_PATTERN_PROOF === '1';
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-pattern-card-'));
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
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-pattern-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

  test('saved CANOPY settings follow each pattern Sound when switching patterns', { timeout: 900_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-pattern-ownership', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const evidence: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
    const { syx, plan } = await prepareRuntimePlanOffline(
      session.options, session.runDir, artifacts,
      ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_admission', 'ip_src_debug', 'src_state',
        ...(proof ? ['canopy_sound_proof'] : [])],
      { modules: [proof ? 'test/instrument-play-proof' : 'core/instrument-play', 'core/digikit-menu',
        'core/health', ...(proof ? ['test/canopy-sound-proof'] : [])] },
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
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, timeout: 180_000 });
    const { tap: key, edge, turn } = acknowledgedPanel(session);
    const selectCanopy = async () => {
      await edge('TRK', true);
      try { await key('TRIG 1'); } finally { await edge('TRK', false); }
      await edge('FUNC', true);
      try { await key('SRC'); } finally { await edge('FUNC', false); }
      for (let i = 0; i < 20; i++) await key('DOWN');
      await key('YES');
      await key('YES');
      await expect.poll(async () =>
        (await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)))).includes(0),
      { timeout: 60_000 }).toBe(true);
      await key('SRC');
      await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(0);
      for (let i = 0; i < 4 && (await cf('ip_src_debug', 1)) !== 0; i++) await key('SRC');
      await expect.poll(() => cf('ip_src_debug', 2), { timeout: 30_000 }).toBeGreaterThan(0);
    };
    const values = async () => {
      const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
      const instance = tracks.indexOf(0);
      if (instance < 0) throw new Error(`Track 1 is not admitted: ${tracks}`);
      const base = instance * 48;
      const [metadata, first, strum] = await Promise.all([
        cf('src_state', base + 6), cf('src_state', base + 7), cf('src_state', base + 9),
      ]);
      return { mode: metadata & 3, chord: first >>> 16, voicing: first & 0xffff, strum: strum >>> 16 };
    };
    const kit = () => word(0x80004704); // DTII 1.17 active kit pointer (devices/digitakt-ii.toml).
    const selectPattern = async (trig: 1 | 2) => {
      await edge('PTN', true);
      try { await key(`TRIG ${trig}`); } finally { await edge('PTN', false); }
    };
    const changePattern = async (trig: 1 | 2, expectedKit: number) => {
      await selectPattern(trig);
      await expect.poll(kit, { timeout: 60_000 }).toBe(expectedKit);
      await waitEmulated(session, 1000);
      await expect.poll(async () =>
        (await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)))).includes(0),
      { timeout: 60_000 }).toBe(true);
    };

    const kit1 = await kit();
    await selectCanopy();
    await turn(2, 20);
    await turn(3, 12);
    await turn(4, 16);
    await expect.poll(values, { timeout: 30_000 }).toMatchObject({ mode: 1, voicing: 3 });
    evidence.first = await values();
    evidence.kit1 = kit1;

    await selectPattern(2);
    await expect.poll(kit, { timeout: 60_000 }).not.toBe(kit1);
    const kit2 = await kit();
    evidence.kit2 = kit2;
    await selectCanopy();
    await turn(2, 12);
    await turn(3, 8);
    await turn(4, 8);
    evidence.second = await values();
    expect(evidence.second).not.toEqual(evidence.first);

    // Project save invokes the native writer for both kits, preserving any
    // earlier snapshots before testing ownership transitions within this boot.
    await edge('FUNC', true);
    try { await key('SETTINGS'); } finally { await edge('FUNC', false); }
    await waitEmulated(session, 300);
    await key('YES');
    await waitEmulated(session, 3500);
    if (proof) evidence.proofAfterSave = await Promise.all(Array.from({ length: 8 }, (_, i) => cf('canopy_sound_proof', i)));

    await changePattern(1, kit1);
    evidence.returnedFirst = await values();
    expect(evidence.returnedFirst, 'A01 restores its own saved CANOPY settings').toEqual(evidence.first);
    await changePattern(2, kit2);
    evidence.returnedSecond = await values();
    expect(evidence.returnedSecond, 'A02 restores its own saved CANOPY settings').toEqual(evidence.second);
    await session.expectNoFault('CANOPY pattern Sound ownership');
  });
} else {
  test.skip('CANOPY pattern ownership experiment is opt-in', () => {});
}
