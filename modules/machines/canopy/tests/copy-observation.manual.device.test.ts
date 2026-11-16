import fs from 'node:fs';
import os from 'node:os';
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
import { moduleArtifact } from '@digi/regression/runtime/install.ts';

// Unregistered emulator-only snapshot for copy tracing. The experimental
// modules are unsafe for hardware. A snapshot alone proves no ownership.
vi.setConfig({ hookTimeout: 600_000 });
const enabled = process.env.DIGI_CANOPY_COPY_TRACE === '1';
if (enabled) {
const options = liveOptions('dt2', { fresh: true });
const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
const cardSeed = enabled ? fs.mkdtempSync(path.join(fixtureRoot, 'canopy-copy-trace-card-')) : undefined;
const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
beforeAll(() => {
  if (!cardSeed) return;
  for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
    fs.copyFileSync(path.join(options.card!, name), path.join(cardSeed, name));
  const modules = path.join(cardSeed, 'digikit', 'modules');
  fs.mkdirSync(modules, { recursive: true });
  fs.copyFileSync(moduleFile, path.join(modules, 'canopy.dkm'));
});
afterAll(() => {
  if (!cardSeed) return;
  const owned = fs.realpathSync(cardSeed);
  if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-copy-trace-card-'))
    throw new Error('Refusing to remove an unowned card fixture');
  fs.rmSync(owned, { recursive: true });
});
const h = withSession({ ...options, card: cardSeed, dsp: 'sharc', restart: false });

test.skipIf(!enabled)('save an edited CANOPY state for Sound-copy instruction tracing',
  { timeout: 900_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-copy-trace', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const evidence: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
    const { syx, plan } = await prepareRuntimePlanOffline(
      session.options, session.runDir, artifacts,
      ['DKRT_BOOT', 'ip_control', 'ip_tracks', 'ip_admission', 'ip_src_debug', 'src_state', 'canopy_sound_proof', 'canopy_copy_probe_match'],
      { modules: ['test/instrument-play-proof', 'core/digikit-menu', 'core/health', 'test/canopy-sound-proof', 'test/canopy-copy-probe'] },
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

    // Keep the exact base card and the complete CF state on the session's
    // temporary filesystem. The state includes card writes made during setup;
    // restore checks the base card's identity and digest. Never fall back to
    // copying the enormous sparse card onto the workspace filesystem.
    const card = session.cardPath;
    if (!card) throw new Error('Trace needs the exact base card image');
    if (fs.statSync(card).dev !== fs.statSync(os.tmpdir()).dev)
      throw new Error('Trace requires the base card and temporary directory on the same filesystem');
    // Deliberately retained after the test for the separate trace; this is a
    // new, uniquely named directory, never a user-provided path.
    const traceDir = fs.mkdtempSync(path.join(os.tmpdir(), 'canopy-copy-trace-'));
    const savedCard = path.join(traceDir, 'card.img');
    fs.linkSync(card, savedCard);
    const savedSyx = path.join(traceDir, 'runtime.syx');
    fs.copyFileSync(syx, savedSyx);
    const state = path.join(traceDir, 'edited.state');
    await session.client.call('state.save', { path: state });
    const active = await instance();
    evidence.trace = {
      state, card: savedCard, syx: savedSyx,
      baseSha256: plan.base.syx_sha256,
      marker: planSymbol(plan, 'canopy_copy_probe_match'),
      srcState: planSymbol(plan, 'src_state'), instance: active,
      sound: await cf('ip_admission', active * 8 + 1),
    };
    console.log(`CANOPY copy trace snapshot: ${artifacts}`);
    await session.expectNoFault('CANOPY Sound copy trace snapshot');
  });
} else {
  test.skip('CANOPY Sound copy observation proof is opt-in', () => {});
}
