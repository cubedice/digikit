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

describeOn('dt2', 'CANOPY pattern copy on the DT2', () => {
  // CANOPY Sound persistence (finding 151): two CANOPY tracks on two patterns
  // with different settings, a project save, a return to the first pattern, then
  // pattern copy, a further source edit, paste and undo with the stock panel.
  vi.setConfig({ hookTimeout: 600_000 });
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

  test('copying and undoing a pattern preserves each CANOPY Sound snapshot', { timeout: 900_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-pattern-copy', path.basename(session.runDir));
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
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, timeout: 180_000 });
    const { tap: key, edge, turn } = acknowledgedPanel(session);
    const focusTrack = async (track: number) => {
      await edge('TRK', true);
      try { await key(`TRIG ${track}`); } finally { await edge('TRK', false); }
    };
    const selectCanopy = async (track = 1) => {
      await focusTrack(track);
      await edge('FUNC', true);
      try { await key('SRC'); } finally { await edge('FUNC', false); }
      for (let i = 0; i < 20; i++) await key('DOWN');
      await key('YES');
      await key('YES');
      await expect.poll(async () =>
        (await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)))).includes(track - 1),
      { timeout: 60_000 }).toBe(true);
      await key('SRC');
      await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(track - 1);
      for (let i = 0; i < 4 && (await cf('ip_src_debug', 1)) !== 0; i++) await key('SRC');
      await expect.poll(() => cf('ip_src_debug', 2), { timeout: 30_000 }).toBeGreaterThan(0);
    };
    const values = async (track = 1) => {
      const tracks = await Promise.all(Array.from({ length: 4 }, (_, i) => cf('ip_tracks', i)));
      const instance = tracks.indexOf(track - 1);
      if (instance < 0) throw new Error(`Track ${track} is not admitted: ${tracks}`);
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
    await expect.poll(() => values(), { timeout: 30_000 }).toMatchObject({ mode: 1, voicing: 3 });
    evidence.first = await values();
    evidence.kit1 = kit1;
    await selectCanopy(2);
    await turn(2, 12); await turn(3, 8); await turn(4, 8);
    evidence.firstTrack2 = await values(2);
    await focusTrack(1);
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(0);

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
    await selectCanopy(2);
    await turn(2, 24); await turn(3, 12); await turn(4, 12);
    evidence.secondTrack2 = await values(2);
    expect(evidence.secondTrack2).not.toEqual(evidence.firstTrack2);
    await focusTrack(1);
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(0);

    // Seed the two distinct sounds before exercising the pattern clipboard.
    await edge('FUNC', true);
    try { await key('SETTINGS'); } finally { await edge('FUNC', false); }
    await waitEmulated(session, 300);
    await key('YES');
    await waitEmulated(session, 3500);

    await changePattern(1, kit1);
    await expect.poll(() => values(), { timeout: 30_000 }).toEqual(evidence.first);
    await expect.poll(() => values(2), { timeout: 30_000 }).toEqual(evidence.firstTrack2);
    await focusTrack(2);
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(1);
    await turn(2, 4);
    evidence.copyTimeTrack2 = await values(2);
    expect(evidence.copyTimeTrack2).not.toEqual(evidence.firstTrack2);
    await focusTrack(1);
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(0);
    // Change the source after its last saved snapshot; the copy must capture
    // this new value, not the Sound handle left by the earlier pattern switch.
    await turn(2, 4);
    evidence.copyTime = await values();
    expect(evidence.copyTime).not.toEqual(evidence.first);
    const modified = async (modifier: string, name: string) => {
      await edge(modifier, true);
      try { await key(name); } finally { await edge(modifier, false); }
    };
    const counters = async () => Promise.all(Array.from({ length: 9 }, (_, i) => cf('ip_sa_stats', i)));
    evidence.beforeCopy = await counters();
    await modified('FUNC', 'RECORD');
    await waitEmulated(session, 1000);
    evidence.afterCopy = await counters();
    // A copy must retain the values at copy time, even if the source changes.
    await turn(2, 4);
    evidence.afterSourceEdit = await values();
    expect(evidence.afterSourceEdit).not.toEqual(evidence.copyTime);
    await focusTrack(2);
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(1);
    await turn(2, 4);
    expect(await values(2)).not.toEqual(evidence.copyTimeTrack2);
    await focusTrack(1);
    await expect.poll(() => cf('ip_src_debug', 0), { timeout: 30_000 }).toBe(0);
    await changePattern(2, kit2);
    evidence.beforePaste = await values();
    await modified('FUNC', 'STOP');
    await waitEmulated(session, 2000);
    evidence.afterPaste = await values();
    evidence.afterPasteCounters = await counters();
    await expect.poll(() => values(2), { timeout: 30_000 }).toEqual(evidence.copyTimeTrack2);
    evidence.afterPasteTrack2 = await values(2);
    // FUNC+STOP a second time performs stock undo paste.
    await modified('FUNC', 'STOP');
    await waitEmulated(session, 2000);
    evidence.afterUndo = await values();
    evidence.afterUndoCounters = await counters();
    await expect.poll(() => values(2), { timeout: 30_000 }).toEqual(evidence.secondTrack2);
    evidence.afterUndoTrack2 = await values(2);
    await session.expectNoFault('CANOPY pattern copy and undo');
    expect.soft(evidence.beforePaste, 'A02 retains its independent settings before paste').toEqual(evidence.second);
    expect.soft(evidence.afterPaste, 'pasted Sound reflects the copied A01 settings').toEqual(evidence.copyTime);
    expect.soft(evidence.afterUndo, 'undo restores A02 settings').toEqual(evidence.second);
  });
});
