import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, onTestFinished, test, vi } from 'vitest';
import { repoRoot } from '@digi/test-support';
import { withSession } from '@digi/regression/session-handle.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { liveOptions } from '@digi/regression/runtime/live-options.ts';
import { liveSampleFolder, seededCard } from '@digi/regression/runtime/live-card.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanListedOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { instrumentSymbols } from '@digi/regression/runtime/instrument-symbols.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { moduleArtifact } from '@digi/regression/runtime/module-artifact.ts';
import { seedModules } from '@digi/regression/runtime/card-seed.ts';
import { waitEmulated } from '@digi/regression/runtime/port-compat.ts';
import { canopyPanel } from './canopy-panel.ts';
import { pitches } from './audio.ts';

describeOn('dt2', 'CANOPY pitch bend through the track keyboard', () => {
  // CANOPY has no MIDI route of its own: the track keyboard plays it from the Digitakt's MIDI config, and hands the track's pitch bend
  // (and CC1, CC121) to CANOPY with its notes (tk_canopy_controller). The DSP bends only the voices of the route the bend arrives on, so
  // the notes and the bend must share one: a held note must sound higher after a full bend up.
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-wheel-bend-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(liveSampleFolder('dt2')!, name), path.join(cardSeed, name));
    await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines', 'instrument', 'track-keyboard'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-wheel-bend-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, cardImage: () => seededCard('dt2', options.firmware, cardSeed!), dsp: 'sharc', restart: false });

  test('a full bend up on track 1 channel raises the held CANOPY note', { timeout: 900_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-wheel-bend', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const evidence: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
    const { syx, plan } = await prepareRuntimePlanListedOffline(session.options, session.runDir, artifacts, ['DKRT_BOOT'], {
      modules: ['core/instrument-play', 'core/track-keyboard', 'core/health'],
    });
    await session.client.firmwareLoad({ syx, base_sha256: plan.base.syx_sha256, start: 'auto', force: true, auto_no: true, card: session.cardPath });
    await session.client.call('run.mode', { mode: 'free' });
    const dk = liveProtocol(session);
    expect(await dk.verifyFile('modules/canopy.dkm', fs.readFileSync(moduleFile))).toBe(true);
    await dk.select(['modules/canopy.dkm']);
    const epoch = Number((await session.status()).epoch);
    await dk.restart();
    await dk.waitForBoot();
    const sym = await instrumentSymbols(session, plan);
    const panel = canopyPanel(session, plan, sym);
    await waitInstrumentReady(panel.cf);
    await expectRuntimeUi(session, plan, artifacts, { afterEpoch: epoch, emulatedMs: 180_000 });
    await panel.chooseCanopy(1);
    const lanes = async () => {
      const addr = sym('track_keyboard_lanes');
      const value = await session.client.memPeek((addr & ~3) >>> 0);
      return value === null ? null : (value >>> (24 - 8 * (addr & 3))) & 255;
    };
    await expect.poll(lanes, { timeout: 30_000, message: 'the track keyboard plays track 1' }).not.toBe(0);

    const midi = (bytes: number[]) => session.client.call('midi.usb.in', { bytes });
    const capture = async (name: string) => {
      const file = path.join(artifacts, `${name}.f32le`);
      await session.client.call('pcm.tap', { action: 'capture', path: file });
      try {
        await waitEmulated(session, 700);
      } finally {
        await session.client.call('pcm.tap', { action: 'capture_stop' });
      }
      return fs.readFileSync(file);
    };
    // the strongest spectral peak of the capture, as a MIDI note number
    const peak = (raw: Buffer) => pitches(raw, 0, 16384, 1)[0]!;

    // Channel 1 plays track 1 in the Digitakt's default MIDI config.
    await midi([0x90, 57, 100]);
    await waitEmulated(session, 400);
    const held = await capture('held');
    await midi([0xe0, 0x7f, 0x7f]);
    await waitEmulated(session, 200);
    const bent = await capture('bent');
    await midi([0xe0, 0x00, 0x40]);
    await midi([0x80, 57, 0]);
    evidence.held = peak(held);
    evidence.bent = peak(bent);
    expect(evidence.held, 'the held note sounds').toBeGreaterThan(40);
    expect(peak(bent) - peak(held), 'a full bend up raises the note').toBeGreaterThan(0.5);
    await session.expectNoFault('CANOPY pitch bend through the track keyboard');
  });
});
