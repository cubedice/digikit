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
  rpc,
  word,
} from '@digi/regression/runtime/fx-live.ts';
import { SINE } from '@digi/regression/runtime/fixtures.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, selectSine, shot, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { selectTrack } from '@digi/regression/runtime/fx-track-ui.ts';
import { record } from '@digi/regression/runtime/artifacts.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { symbolsOf } from '@digi/regression/runtime/service-symbols.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import {
  PH,
  advance,
  chooseTracks,
  float,
  holdingG,
  mainPage,
  openPages,
  openPrismPage,
  pressTrig,
  prismPage,
  setPrism,
  tap,
  trackMask,
} from './ui-helpers.ts';

/** A track PRISM hears feeds it at full send: the send law's gain is the amount squared over 127 squared (finding 62), 1 at 127. */
const AMOUNT = PH.PH_SEND_FULL!;
const SEND_GAIN = (AMOUNT / 127) ** 2;
/** Frames of each capture: the echo's time (60 ms) and the part the level is measured on fit with room. */
const LENGTH = 9000;
/** The owner's device: the fx service's record of PRISM's send amounts from before the TRIG keys chose its tracks, 127 for tracks 1 and 11. */
const OWNER_SENDS = Array.from({ length: 16 }, (_, t) => (t === 0 || t === 10 ? '7f' : '00')).join('');

// PRISM as a send effect on the patched firmware: its input is the tracks it hears (chosen with the TRIG keys while G is held on its page) at
// full send, its output is added to the FX return, and the dry sound is not touched. The engine's own behaviour is the rig's
// (tests/engine.rig.test.ts); the page model is the host harness's (core/phrase-ui); this proves the key path, the bus wiring and the carry-over.
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
      symbols: [
        'DKRT_BOOT',
        'fx.rows',
        'phrase_ui_debug',
        'phrase_ui_values',
        'phrase_ui_page',
        '_phrase_control_info',
        '_phrase_status',
      ],
      policy: 'fx-routes',
    },
    initial: 'prism',
    presets: {
      prism: { selection: ['modules/m0.dkm', 'modules/m1.dkm'] },
      // the same card without PRISM in the selection: the dry reference
      without: { selection: ['modules/m0.dkm'] },
      // the owner's device: PRISM's send amounts as the fx service kept them, and no tracks of PRISM's own
      carried: { selection: ['modules/m0.dkm', 'modules/m1.dkm'], settings: [['fx/send/digikit/prism', OWNER_SENDS]] },
    },
    requireAudible: true,
  });

  test('G and the TRIG keys choose the tracks PRISM hears: a chosen track is echoed after TIME at full send, another is not, the keys play no note and enter no step, and the old send amounts carry over', async () => {
    const { fixture, plan, profile, artifacts } = context;
    let runtime = fixture.runtime;
    let session = runtime.session;
    const boot = await inspectFxBoot(runtime, plan, [SINE, 'digikit/prism'], artifacts, 'boot');
    context.rememberEpoch(boot.epoch);
    const values = planSymbol(plan, 'phrase_ui_values');
    const debug = planSymbol(plan, 'phrase_ui_debug');
    const tracksNow = () => word(session, values + 4 * PH.PH_P_TRKS!);
    const page = () => prismPage(session, planSymbol(plan, 'phrase_ui_page'));
    const status = async () => {
      const words = await dspWords(session, planSymbol(plan, '_phrase_status'), 32);
      return (name: string) => words[PH[name]! / 4]!;
    };
    const sendRow = async () => (await rows(session, plan, context.slots))[0]!.slice(18, 23);
    // A key goes down (and stays down until a release).
    const down = (key: string) => async () => {
      await bounded(() => session.client.inputButton(button(key), true));
    };
    // A note of track 1 or 2 (their TRIG keys) for LENGTH frames, captured at the main output; `during` presses the key itself.
    const capture = async (tag: string, during?: () => Promise<void>) => {
      const file = path.join(artifacts, `${tag}.wav`);
      await captureNote(session, {
        file,
        // the same place in the block schedule for every note, so that two captures of one sound are equal sample for sample
        trigger: Math.ceil(((await frames(session)) + 300) / 256) * 256,
        tag,
        length: LENGTH,
        noPress: during !== undefined,
        during,
      });
      const samples = readFloatWav(file);
      expect(samples.length, `${tag} is captured`).toBeGreaterThan(LENGTH);
      expect(samples.every((sample) => Number.isFinite(sample))).toBe(true);
      // the test instrument sounds for two seconds: let it end, with every tail, before the next capture
      await advance(session, 2600);
      return samples;
    };
    const sound = async (tag: string, track: 1 | 2) => {
      const samples =
        track === 1
          ? await capture(tag)
          : await capture(tag, down('TRIG 2')).finally(() =>
              bounded(() => session.client.inputButton(button('TRIG 2'), false)),
            );
      expect(peak(samples), `${tag}: the TRIG key plays its note`).toBeGreaterThan(0.01);
      return samples;
    };
    // the amount words follow a selection within a few UI passes and the link's confirmation
    const sendRowIs = async (want: number[], what: string) => {
      for (let i = 0; i < 40 && JSON.stringify(await sendRow()) !== JSON.stringify(want); i++) await advance(session, 50);
      expect(await sendRow(), what).toEqual(want);
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

    // PRISM runs on the send bus by itself (its record allows the send route only), with no cell of the send page: the track's FX key has
    // the stock page alone, so there is no PRS cell. Nothing is stored: no track is chosen, and the amounts are 0.
    const sym = symbolsOf(runtime, plan);
    expect(await sendRow(), 'the send route, and no amount: no track').toEqual([1, 0, 0, 0, 0]);
    expect(await word(session, sym('fxp_send_count')), 'the track FX key lists the stock page alone').toBe(0);
    expect(await word(session, sym('DKRT_FXS') + 4), 'no cell of the send page is used').toBe(0);
    await sines();
    await mainPage(session);
    await openPages(session);
    await shot(session, artifacts, 'track-fx-page-1');
    await tap(session, 'FX', 400);
    await shot(session, artifacts, 'track-fx-after-second-fx');
    expect(await word(session, sym('fxp_send_count')), 'still no send page').toBe(0);

    // One echo and nothing else: zone SLAP at its short end (60 ms), no repeats, MIX full; COLR, HALO and SKEW rest at their defaults.
    await openPrismPage(session, debug);
    expect(await tracksNow(), 'nothing stored: no track').toBe(0);
    expect((await page()).cells[6], 'INPT says no track is heard').toMatchObject({ value: 'LIVE', state: 'NO TRK' });
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

    // With no track chosen: the two tracks as they are.
    const second = await sound('track2-none', 2);
    const first = await sound('track1-none', 1);

    // On PRISM's page, G held and TRIG 1: track 1 is heard at full send. The press plays no note: the capture holds nothing.
    await openPrismPage(session, debug);
    const taken = await word(session, debug + 36);
    let quiet: Float32Array | undefined;
    await holdingG(session, async () => {
      expect(await word(session, debug + 32), 'G holds the selection').toBe(1);
      expect((await page()).cells[6]).toMatchObject({ value: 'NO TRK', state: 'TRIGS', lit: true });
      quiet = await capture('trig1-with-g', down('TRIG 1'));
      expect((await page()).cells[6]).toMatchObject({ value: '1 TRK', state: 'TRIGS' });
      await shot(session, artifacts, 'g-held-track-1');
    });
    const dispatched = (await word(session, debug + 36)) - taken;
    record(artifacts, 'trig-with-g', { peak: peak(quiet!), dispatched });
    console.info('PRISM TRIG 1 with G held:', JSON.stringify({ peak: peak(quiet!), dispatched, dryPeak: peak(first) }));
    expect(dispatched, 'the key dispatcher gave the TRIG key to the page, its press and its release').toBeGreaterThanOrEqual(2);
    expect(peak(quiet!), 'TRIG 1 with G held plays no note').toBeLessThan(1e-4);
    expect(await tracksNow()).toBe(trackMask([1]));
    await sendRowIs([1, AMOUNT, 0, 0, 0], 'track 1 at full send in the DSP row');
    expect((await page()).cells[6], 'G released: INPT, and under it the tracks').toMatchObject({ value: 'LIVE', state: '1 TRK' });

    // In GRID RECORD, G held and TRIG 2 twice (on and off again): no step is entered; a cycle of the pattern plays nothing.
    // the RECORD key's LED (panel index 9 in the LED map of the device file) is lit while grid recording is on: 0x01 is off
    // (before the first press the panel may have no value for it yet)
    const recordLed = async () => parseInt((await session.leds()).leds.slice(18, 20), 16);
    expect([0x00, 0x01], 'grid recording is off').toContain(await recordLed());
    await tap(session, 'RECORD', 200);
    const recordOn = await recordLed();
    expect([0x00, 0x01], 'grid recording is on').not.toContain(recordOn);
    expect(await word(session, debug + 24), "RECORD leaves PRISM's page on top").not.toBe(0);
    await holdingG(session, async () => {
      await pressTrig(session, 2);
      expect(await tracksNow()).toBe(trackMask([1, 2]));
      await pressTrig(session, 2);
    });
    await tap(session, 'RECORD', 200);
    expect(await recordLed(), 'grid recording is off again').toBe(0x01);
    expect(await tracksNow()).toBe(trackMask([1]));
    const cycle = path.join(artifacts, 'cycle-after-record.wav');
    await rpc(session, 'pcm.tap', { action: 'start' });
    await rpc(session, 'pcm.tap', { action: 'capture', path: cycle });
    try {
      await tap(session, 'PLAY', 80);
      await advance(session, 2400);
    } finally {
      await rpc(session, 'pcm.tap', { action: 'capture_stop' });
      await tap(session, 'STOP', 200);
    }
    const played = readFloatWav(cycle);
    record(artifacts, 'cycle-after-record', { peak: peak(played), samples: played.length });
    console.info('PRISM a cycle after G and TRIG 2 in GRID RECORD:', JSON.stringify({ peak: peak(played), samples: played.length }));
    expect(played.length, 'a cycle is captured').toBeGreaterThan(2 * 48000);
    expect(peak(played), 'no step was entered: the pattern plays nothing').toBeLessThan(1e-4);
    await mainPage(session);
    await advance(session, 300);

    // Track 2 is not heard: its sound through PRISM is the same sample for sample. Its TRIG key plays its note again (`sound`).
    const other = await sound('track2-unchosen', 2);
    const unheard = alignedCompare(second, other);
    record(artifacts, 'track2-unchosen-report', unheard);
    expect(unheard.differ, 'a track PRISM does not hear adds nothing through PRISM').toBe(0);

    // Track 1 is heard: its sound comes again TIME frames later at the level of the send law times MIX.
    const sent = await sound('track1-chosen', 1);
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
    // back to no track for the settings on the card
    await openPrismPage(session, debug);
    await chooseTracks(session, values, []);
    await sendRowIs([1, 0, 0, 0, 0], 'no track: no amount');
    await mainPage(session);

    // The same card without PRISM in the selection: track 1 sounds as it did with PRISM loaded and no track chosen.
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

    // The owner's device: fx/send/digikit/prism holds 127 for tracks 1 and 11 and PRISM has no tracks of its own. PRISM hears tracks 1 and
    // 11, and the DSP row has them at full send.
    runtime = await fixture.reload(context.baseline('carried'));
    session = runtime.session;
    const carried = await inspectFxBoot(runtime, plan, [SINE, 'digikit/prism'], artifacts, 'boot-carried');
    context.rememberEpoch(carried.epoch);
    await advance(session, 500);
    await sendRowIs([1, AMOUNT, 0, AMOUNT << 16, 0], 'tracks 1 and 11 at full send: the old send amounts carried over');
    await sines();
    await sound('track1-carried', 1);
    // the page's copy of the values is refreshed while the page is shown
    await openPrismPage(session, debug);
    expect(await tracksNow(), 'the tracks of the old send amounts').toBe(trackMask([1, 11]));
    expect((await page()).cells[6]).toMatchObject({ value: 'LIVE', state: '2 TRK' });
    await shot(session, artifacts, 'carried-2-tracks');
    await mainPage(session);
    await bounded(() => session.expectNoFault('the carried send amounts'));
  }, 600_000);
});
