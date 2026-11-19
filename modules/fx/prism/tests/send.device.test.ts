import path from 'node:path';
import { button } from '@digi/test-support';
import { expect, test } from 'vitest';
import {
  alignedCompare,
  bounded,
  captureNote,
  dspWords,
  frames,
  peak,
  readFloatWav,
  rows,
  REDRAW,
} from '@digi/regression/runtime/fx-live.ts';
import { SINE } from '@digi/regression/runtime/fixtures.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, selectSine, shot, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { amountShown, selectTrack, setAmount } from '@digi/regression/runtime/fx-track-ui.ts';
import { record } from '@digi/regression/runtime/artifacts.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { PH, advance, float, mainPage, openPrismPage, setPrism } from './ui-helpers.ts';

/** The send amount of track 1 in this test, and the gain the send law gives it: the amount squared over 127 squared (finding 62). */
const AMOUNT = 64;
const SEND_GAIN = (AMOUNT / 127) ** 2;
/** Frames of each capture: the echo's time (60 ms) and the part the level is measured on fit with room. */
const LENGTH = 9000;

// PRISM as an ordinary send effect on the patched firmware: its input is the tracks times their PRS send amounts, its output is added to the
// FX return, and the dry sound is not touched. The engine's own behaviour is the rig's (tests/engine.rig.test.ts); this proves the bus wiring.
describeOn('dt2', 'DTII PRISM as a send effect', () => {
  const context = withPreparedFxSession('dt2', 'PRISM send', {
    image: {
      name: 'prism-send',
      modules: [
        'core/runtime',
        'core/dynreg',
        'core/dyndsp',
        'core/dynfx',
        'core/phrase-ui',
        'core/digikit-menu',
        'core/health',
      ],
      inventory: [SINE, 'digikit/prism'],
      symbols: ['DKRT_BOOT', 'fx.rows', 'phrase_ui_debug', 'phrase_ui_values', '_phrase_control_info', '_phrase_status'],
      policy: 'fx-routes',
    },
    initial: 'prism',
    presets: {
      prism: { selection: ['modules/m0.dkm', 'modules/m1.dkm'] },
      // the same card without PRISM in the selection: the dry reference
      without: { selection: ['modules/m0.dkm'] },
    },
    requireAudible: true,
  });

  test('a track with a PRS send amount is echoed after TIME at the send law, a track with none is not heard, and the dry path is the one without PRISM', async () => {
    const { fixture, plan, profile, artifacts } = context;
    let runtime = fixture.runtime;
    let session = runtime.session;
    const boot = await inspectFxBoot(runtime, plan, [SINE, 'digikit/prism'], artifacts, 'boot');
    context.rememberEpoch(boot.epoch);
    const values = planSymbol(plan, 'phrase_ui_values');
    const status = async () => {
      const words = await dspWords(session, planSymbol(plan, '_phrase_status'), 32);
      return (name: string) => words[PH[name]! / 4]!;
    };
    // A note of track 1 or 2 (their TRIG keys) for LENGTH frames, captured at the main output.
    const sound = async (tag: string, track: 1 | 2) => {
      const file = path.join(artifacts, `${tag}.wav`);
      try {
        await captureNote(session, {
          file,
          // the same place in the block schedule for every note, so that two captures of one sound are equal sample for sample
          trigger: Math.ceil(((await frames(session)) + 300) / 256) * 256,
          tag,
          length: LENGTH,
          noPress: track !== 1,
          during: async () => {
            if (track === 2) await bounded(() => session.client.inputButton(button('TRIG 2'), true));
          },
        });
      } finally {
        if (track === 2) await bounded(() => session.client.inputButton(button('TRIG 2'), false));
      }
      const samples = readFloatWav(file);
      expect(samples.length, `${tag} is captured`).toBeGreaterThan(LENGTH);
      expect(samples.every((sample) => Number.isFinite(sample))).toBe(true);
      expect(peak(samples), `${tag} is heard`).toBeGreaterThan(0.01);
      // the test instrument sounds for two seconds: let it end, with every tail, before the next capture
      await advance(session, 2600);
      return samples;
    };
    const sines = async () => {
      // The test instrument on tracks 1 and 2 (the native track-object layout is in the shared profile).
      await selectSine(runtime, profile);
      await mainPage(session);
      await selectTrack(sessionRt(session), 2);
      expect(profile.track_type_stride).toBeGreaterThan(0);
      await selectSine(runtime, { ...profile, track_type_word: profile.track_type_word + profile.track_type_stride! });
      await mainPage(session);
      await selectTrack(sessionRt(session), 1);
    };

    // PRISM takes the first cell of the send page by itself: its record allows the send route only.
    const row = (await rows(session, plan, context.slots))[0]!;
    expect(row[0], 'the effect row is claimed').not.toBe(0xffffffff);
    expect(row.slice(18, 23), 'the send route, and no amount yet').toEqual([1, 0, 0, 0, 0]);
    await sines();

    // One echo and nothing else: zone SLAP at its short end (60 ms), no repeats, MIX full; COLR, HALO and SKEW rest at their defaults.
    await openPrismPage(session, planSymbol(plan, 'phrase_ui_debug'));
    await setPrism(session, values, PH.PH_P_ZONE!, 'ENC A', 3, { detent: true });
    await setPrism(session, values, PH.PH_P_RATE!, 'ENC B', 0, { coarse: PH.PH_RATE_COARSE });
    await setPrism(session, values, PH.PH_P_RPTS!, 'ENC C', 0);
    await setPrism(session, values, PH.PH_P_MIX!, 'LEVEL', 127);
    await mainPage(session);
    await advance(session, 1500);
    const engine = await status();
    const time = engine('PH_S_TIME_L');
    expect(engine('PH_S_ERROR')).toBe(PH.PH_ERROR_NONE);
    expect(engine('PH_S_TIME_R'), 'both sides play the same time').toBe(time);
    expect(time, 'SLAP at RATE 0 is 60 ms').toBe(2880);
    const mix = float((await dspWords(session, planSymbol(plan, '_phrase_control_info'), 64))[PH.PH_C_MIX! / 4]!);
    expect(mix).toBe(1);

    // With every send at 0: the two tracks as they are.
    const second = await sound('track2-sends-0', 2);
    const first = await sound('track1-sends-0', 1);

    // The PRS amount of track 1 on the second page of the track's FX key; track 2 stays at 0.
    const rt = sessionRt(session);
    await rt.ui.tap('FX', { until: { ms: 400, why: REDRAW } });
    await rt.ui.tap('FX', { until: { ms: 400, why: REDRAW } });
    await setAmount(rt, profile, 'ENC A', 0, AMOUNT);
    await shot(session, artifacts, 'track-sends');
    expect(await amountShown(session, profile, 1), 'track 2 has no PRS amount').toBe(0);
    await mainPage(session);
    await advance(session, 300);
    const routed = (await rows(session, plan, context.slots))[0]!;
    expect(routed.slice(18, 23), 'the amount of track 1 is in the DSP row').toEqual([1, AMOUNT, 0, 0, 0]);

    // Track 2 has no amount: PRISM does not hear it, and its sound is the same sample for sample.
    const other = await sound('track2-unsent', 2);
    const unheard = alignedCompare(second, other);
    record(artifacts, 'track2-unsent-report', unheard);
    expect(unheard.differ, 'a track with send 0 adds nothing through PRISM').toBe(0);

    // Track 1 has the amount: its sound comes again TIME frames later at the level of the send law times MIX.
    const sent = await sound('track1-sent', 1);
    const onset = (x: Float32Array) => x.findIndex((v) => Math.abs(v) > 1e-6);
    const [a, b] = [onset(first), onset(sent)];
    expect(a).toBeGreaterThanOrEqual(0);
    expect((b - a) % 2, 'the two captures start on the same channel').toBe(0);
    const n = Math.min(first.length - a, sent.length - b);
    const wet = Float32Array.from({ length: n }, (_, i) => sent[b + i]! - first[a + i]!);
    const dryPeak = peak(first);
    // the first frame where the difference leaves the floor: where the echo starts
    const echoAt = Math.floor(wet.findIndex((v) => Math.abs(v) > 1e-3 * dryPeak) / 2);
    // the echo against the dry sound TIME frames earlier, over 3000 frames: its gain by least squares and what the gain leaves over
    let cross = 0,
      power = 0,
      wetPower = 0;
    const from = 2 * (time + 1024),
      to = 2 * (time + 4024);
    for (let i = from; i < to; i++) {
      const delayed = first[a + i - 2 * time]!;
      cross += wet[i]! * delayed;
      power += delayed * delayed;
      wetPower += wet[i]! * wet[i]!;
    }
    const gain = cross / power;
    let left = 0;
    for (let i = from; i < to; i++) left += (wet[i]! - gain * first[a + i - 2 * time]!) ** 2;
    const report = {
      time,
      echoAt,
      gain,
      expected: SEND_GAIN * mix,
      residual: Math.sqrt(left / wetPower),
      dryPeak,
      // before the echo the sound is the dry one
      early: peak(wet.subarray(0, 2 * (time - 64))),
    };
    record(artifacts, 'echo-report', report);
    console.info('PRISM send echo:', JSON.stringify(report));
    expect(report.early, 'nothing is added before the echo').toBeLessThan(1e-5 * dryPeak);
    expect(Math.abs(echoAt - time), 'the echo starts TIME frames after the note').toBeLessThanOrEqual(64);
    expect(Math.abs(gain / (SEND_GAIN * mix) - 1), 'the level of the send law times MIX').toBeLessThan(0.03);
    expect(report.residual, 'the echo is the dry sound, later').toBeLessThan(0.1);
    const after = (await rows(session, plan, context.slots))[0]!;
    expect(after[12], 'the effect ran').toBe(1);
    expect(after[17], 'the effect guard has no fault').toBe(0);
    await bounded(() => session.expectNoFault('PRISM as a send effect'));

    // The same card without PRISM in the selection: track 1 sounds as it did with PRISM loaded and its sends at 0.
    runtime = await fixture.reload(context.baseline('without'));
    session = runtime.session;
    const plain = await inspectFxBoot(runtime, plan, [SINE], artifacts, 'boot-without');
    context.rememberEpoch(plain.epoch);
    expect(plain.results).toEqual([0]);
    await sines();
    const alone = await sound('track1-without-prism', 1);
    const dry = alignedCompare(alone, first);
    record(artifacts, 'dry-path-report', dry);
    expect(dry.shift, 'both captures hold the note').not.toBeNull();
    expect(dry.differ, 'the dry path with PRISM loaded is the one without it').toBe(0);
    await bounded(() => session.expectNoFault('the card without PRISM'));
  }, 420_000);
});
