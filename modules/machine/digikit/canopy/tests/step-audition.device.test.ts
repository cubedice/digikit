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
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { moduleArtifact } from '@digi/regression/runtime/module-artifact.ts';
import { seedModules } from '@digi/regression/runtime/card-seed.ts';
import { waitEmulated } from '@digi/regression/runtime/port-compat.ts';
import { canopyPanel } from './canopy-panel.ts';
import { envelope } from './audio.ts';

describeOn('dt2', 'CANOPY step entry with KEYBOARD held', () => {
  // Step recording takes notes with KEYBOARD held while keyboard mode is off (finding 349). CANOPY has no stock voice to audition
  // with, so the panel takes such a press as a keyboard note and the track keyboard sounds it (ip_panel_on). The entered note must
  // be heard while its key is held, and stop with the key.
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-step-audition-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(liveSampleFolder('dt2')!, name), path.join(cardSeed, name));
    await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines', 'instrument', 'track-keyboard'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-step-audition-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, cardImage: () => seededCard('dt2', options.firmware, cardSeed!), dsp: 'sharc', restart: false });

  test('a note entered with KEYBOARD held sounds while its key is held', { timeout: 900_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-step-audition', path.basename(session.runDir));
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
    const { tap, edge } = acknowledgedPanel(session);

    // the level of each 10 ms frame of a capture (dB)
    const capture = async (name: string, play: () => Promise<void>) => {
      const file = path.join(artifacts, `${name}.f32le`);
      await session.client.call('pcm.tap', { action: 'capture', path: file });
      try {
        await play();
      } finally {
        await session.client.call('pcm.tap', { action: 'capture_stop' });
      }
      return envelope(fs.readFileSync(file));
    };
    const peak = (env: number[]) => Math.max(...env);
    // The reference: a press in keyboard mode, without recording, sounds this CANOPY track.
    await tap('KEYBOARD');
    const plain = await capture('plain', async () => {
      await edge('TRIG 9', true);
      await waitEmulated(session, 600);
      await edge('TRIG 9', false);
    });
    await tap('KEYBOARD');
    await waitEmulated(session, 3000);
    evidence.plainPeak = peak(plain);

    // Step recording (RECORD + STOP), then a trig key with KEYBOARD held.
    await edge('RECORD', true);
    try {
      await tap('STOP');
    } finally {
      await edge('RECORD', false);
    }
    await edge('KEYBOARD', true);
    let held: number[] = [];
    let released: number[] = [];
    try {
      held = await capture('held', async () => {
        await edge('TRIG 9', true);
        await waitEmulated(session, 600);
      });
      released = await capture('released', async () => {
        await edge('TRIG 9', false);
        // CANOPY's release tail lasts about 2.5 s: the last 0.5 s of 4.5 s is past it
        await waitEmulated(session, 4500);
      });
    } finally {
      await edge('TRIG 9', false);
      await edge('KEYBOARD', false);
    }
    await tap('RECORD');
    evidence.heldPeak = peak(held);
    evidence.releasedTail = peak(released.slice(-50));
    expect(evidence.plainPeak, 'a keyboard-mode press sounds the track (the reference)').toBeGreaterThan(-50);
    expect(peak(held), 'the entered note sounds while its key is held').toBeGreaterThan(-50);
    expect(peak(released.slice(-50)), 'and its release ends it').toBeLessThan(-60);
    await session.expectNoFault('CANOPY step entry with KEYBOARD held');
  });
});
