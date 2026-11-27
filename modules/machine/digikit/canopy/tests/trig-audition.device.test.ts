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
import { planSymbol, prepareRuntimePlanListedOffline } from '@digi/regression/runtime/runtime-plan.ts';
import { instrumentSymbols } from '@digi/regression/runtime/instrument-symbols.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { moduleArtifact } from '@digi/regression/runtime/module-artifact.ts';
import { seedModules } from '@digi/regression/runtime/card-seed.ts';
import { waitEmulated } from '@digi/regression/runtime/port-compat.ts';
import { canopyPanel } from './canopy-panel.ts';
import { envelope } from './audio.ts';

describeOn('dt2', 'CANOPY TRIG press outside keyboard mode', () => {
  // Outside keyboard mode a TRIG key auditions its track (the stock performance handler). CANOPY has no stock voice to audition
  // with, so on a CANOPY track the panel hands the press to the track keyboard (tk_panel_trig_on). Before CANOPY became a voice host
  // its own engine sounded such a press at the level of a keyboard press; the press must sound at that level while held, and stop
  // with its key.
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const cardSeed = fs.mkdtempSync(path.join(fixtureRoot, 'canopy-trig-audition-card-'));
  const moduleFile = moduleArtifact('dt2', 'digikit/canopy');
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(liveSampleFolder('dt2')!, name), path.join(cardSeed, name));
    await seedModules(cardSeed, 'dt2', [{ file: moduleFile, name: 'canopy.dkm', needs: ['machines', 'instrument', 'track-keyboard'] }]);
  });
  afterAll(() => {
    const owned = fs.realpathSync(cardSeed);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('canopy-trig-audition-card-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const h = withSession({ ...options, cardImage: () => seededCard('dt2', options.firmware, cardSeed!), dsp: 'sharc', restart: false });

  test('a TRIG press on a CANOPY track sounds while held and its release ends it', { timeout: 900_000 }, async () => {
    const session = h.session;
    const artifacts = path.join(repoRoot(), 'out/canopy-trig-audition', path.basename(session.runDir));
    fs.mkdirSync(artifacts, { recursive: true });
    const evidence: Record<string, unknown> = {};
    onTestFinished(() => fs.writeFileSync(path.join(artifacts, 'probe.json'), JSON.stringify(evidence, null, 2)));
    const { syx, plan } = await prepareRuntimePlanListedOffline(session.options, session.runDir, artifacts, ['DKRT_BOOT', 'ip_panel_press'], {
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
    // the last press the panel handed the track keyboard: key, track, pitch, velocity as the stock caller passed them
    const pressed = () =>
      Promise.all([0, 1, 2, 3].map(async (i) => ((await session.client.memPeek(planSymbol(plan, 'ip_panel_press') + 4 * i)) ?? -1) >>> 0));
    // The reference: a press in keyboard mode sounds this CANOPY track.
    await tap('KEYBOARD');
    const keyboard = await capture('keyboard', async () => {
      await edge('TRIG 9', true);
      await waitEmulated(session, 600);
      await edge('TRIG 9', false);
    });
    evidence.keyboardPress = await pressed();
    await tap('KEYBOARD');
    await waitEmulated(session, 4500);
    evidence.keyboardPeak = peak(keyboard);

    // TRIG 1 outside keyboard mode: the press of track 1, CANOPY's.
    let held: number[] = [];
    let released: number[] = [];
    try {
      held = await capture('held', async () => {
        await edge('TRIG 1', true);
        await waitEmulated(session, 600);
      });
      evidence.trigPress = await pressed();
      released = await capture('released', async () => {
        await edge('TRIG 1', false);
        // CANOPY's release tail lasts about 2.5 s: the last 0.5 s of 4.5 s is past it
        await waitEmulated(session, 4500);
      });
    } finally {
      await edge('TRIG 1', false);
    }
    evidence.heldPeak = peak(held);
    // the last 100 ms of the 600 ms hold
    evidence.heldEnd = Math.min(...held.slice(-10));
    evidence.releasedTail = peak(released.slice(-50));
    expect(evidence.keyboardPeak, 'a keyboard-mode press sounds the track (the reference)').toBeGreaterThan(-50);
    expect((evidence.trigPress as number[]).slice(0, 2), 'the track keyboard took TRIG 1 of track 1').toEqual([0, 0]);
    expect(peak(held), 'the TRIG press sounds while its key is held').toBeGreaterThan(-50);
    // The track keyboard's focus outside keyboard mode (IP_NONE) once cancelled the press at the panel's next update: the note
    // peaked 6 dB below a keyboard press and was in its release tail while the key was still held.
    expect(peak(held), 'at the level of a keyboard-mode press').toBeGreaterThan((evidence.keyboardPeak as number) - 3);
    expect(evidence.heldEnd, 'and it holds while its key is held').toBeGreaterThan(peak(held) - 3);
    expect(peak(released.slice(-50)), 'and its release ends it').toBeLessThan(-60);
    await session.expectNoFault('CANOPY TRIG press outside keyboard mode');
  });
});
