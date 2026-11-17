import path from 'node:path';
import { button, encoder } from '@digi/test-support';
import { expect, test } from 'vitest';
import { bounded, captureNote, dspWords, frames, readFloatWav, rows, word } from '@digi/regression/runtime/fx-live.ts';
import { SINE } from '@digi/regression/runtime/fixtures.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, selectSine, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { selectTrack } from '@digi/regression/runtime/fx-track-ui.ts';
import { record } from '@digi/regression/runtime/artifacts.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { advance, mainPage, openPrismPage, tap } from './ui-helpers.ts';

const float = (bits: number) => {
  const bytes = Buffer.alloc(4);
  bytes.writeUInt32LE(bits);
  return bytes.readFloatLE();
};
const metrics = (file: string) => {
  const samples = readFloatWav(file);
  let peak = 0,
    energy = 0,
    nonfinite = 0;
  for (const sample of samples) {
    if (!Number.isFinite(sample)) {
      nonfinite++;
      continue;
    }
    peak = Math.max(peak, Math.abs(sample));
    energy += sample * sample;
  }
  return { samples: samples.length, peak, rms: Math.sqrt(energy / Math.max(1, samples.length)), nonfinite };
};

describeOn('dt2', 'DTII PRISM capture through the patched audio path', () => {
  const context = withPreparedFxSession('dt2', 'PRISM capture', {
    image: {
      name: 'prism-capture',
      modules: [
        'core/runtime',
        'core/dynreg',
        'core/dyndsp',
        'core/dynfx',
        'core/phrase-ui',
        'core/phrase-route',
        'core/digikit-menu',
        'core/health',
      ],
      inventory: [SINE, 'digikit/prism'],
      symbols: [
        'DKRT_BOOT',
        'fx.rows',
        'phrase_ui_debug',
        'phrase_ui_values',
        '_phrase_control_info',
        '_phrase_status',
      ],
      policy: 'settings',
    },
    initial: 'phrase',
    presets: { phrase: { selection: ['modules/m0.dkm', 'modules/m1.dkm'] } },
    requireAudible: true,
  });

  test('records one stock-clock step, retains it after stop, and replaces only its held sources', async () => {
    const { fixture, plan, profile, artifacts } = context;
    const runtime = fixture.runtime;
    const session = runtime.session;
    const boot = await inspectFxBoot(runtime, plan, [SINE, 'digikit/prism'], artifacts, 'boot');
    context.rememberEpoch(boot.epoch);
    const values = planSymbol(plan, 'phrase_ui_values');
    const status = () => dspWords(session, planSymbol(plan, '_phrase_status'), 32);
    const control = () => dspWords(session, planSymbol(plan, '_phrase_control_info'), 64);
    const value = (index: number) => word(session, values + index * 4);
    const set = async (index: number, knob: string, target: number, discrete = false) => {
      for (let attempt = 0; attempt < 40; attempt++) {
        const current = await value(index);
        if (current === target) return;
        const delta = discrete ? Math.sign(target - current) * 4 : Math.max(-8, Math.min(8, target - current));
        await bounded(() => session.client.inputTurn(encoder(knob), delta));
        await advance(session, 40);
      }
      expect(await value(index), `${knob} reaches ${target}`).toBe(target);
    };
    const sound = async (tag: string, note?: number, during?: () => Promise<void>, length = 650) => {
      const file = path.join(artifacts, `${tag}.wav`);
      try {
        await captureNote(session, {
          file,
          trigger: (await frames(session)) + 300,
          tag,
          length,
          noPress: note !== 1,
          during: async () => {
            if (note === 2) await bounded(() => session.client.inputButton(button('TRIG 2'), true));
            await during?.();
          },
        });
      } finally {
        if (note === 2) await bounded(() => session.client.inputButton(button('TRIG 2'), false));
      }
      const result = metrics(file);
      record(artifacts, `${tag}-audio`, result);
      expect(result.samples).toBeGreaterThan(1000);
      expect(result.nonfinite).toBe(0);
      return result;
    };

    // Only the declared test instrument supplies audio. All edits and capture
    // gestures use native controls; no CF/DSP memory is poked to fake a take.
    await selectSine(runtime, profile);
    await mainPage(session);
    await selectTrack(sessionRt(session), 2);
    expect(profile.track_type_stride, 'native track-object layout is supplied by the shared profile').toBeGreaterThan(
      0,
    );
    await selectSine(runtime, { ...profile, track_type_word: profile.track_type_word + profile.track_type_stride! });
    await mainPage(session);
    await selectTrack(sessionRt(session), 1);
    await tap(session, 'PLAY', 80);
    await openPrismPage(session, planSymbol(plan, 'phrase_ui_debug'));
    expect(await value(21), 'only track 1 is selected for capture').toBe(1);
    await set(2, 'ENC C', 1); // TAKE = one actual master-step interval.
    await tap(session, 'DOWN', 80);
    await set(6, 'ENC A', 96); // Long, overlapping grains make the wet return observable.
    await set(7, 'ENC B', 5, true);
    await tap(session, 'UP', 80);
    const clock = await control();
    expect(clock[13]! & 7, 'validated running stock tempo').toBe(7);
    expect(clock[57]).toBeGreaterThanOrEqual(3600);

    const live = await sound(
      'live-and-capture',
      1,
      async () => {
        await set(0, 'ENC A', 1, true);
      },
      1000,
    );
    expect(live.peak).toBeGreaterThan(0.01);
    let held = await status();
    expect(held[4], 'capture has no error').toBe(0);
    expect(held[5], 'held source membership').toBe(1);
    expect(held[8], 'one capture completed').toBeGreaterThan(0);
    expect(held[11], 'resolved TAKE').toBe(1);
    expect(held[20], 'one-shot recording returned to OFF').toBe(0);
    expect(await value(0)).toBe(0);
    expect(held[31]).toBe(held[2]);
    expect(held[24]).toBe((await control())[56]);
    // The clean native card uses the stock 1x master scale. Block-quantized
    // capture may round the 48 kHz sixteenth-note interval by at most 32 frames.
    const expectedFrames = 86_400_000 / clock[57]!;
    expect(held[8]).toBeGreaterThanOrEqual(Math.floor(expectedFrames) - 32);
    expect(held[8]).toBeLessThanOrEqual(Math.ceil(expectedFrames) + 32);
    record(artifacts, 'committed-take', { clock, held, expectedFrames });

    await tap(session, 'STOP', 80);
    await advance(session, 2200); // The sine fixture ends by itself after two seconds.
    const committed = held[8];
    await set(20, 'LEVEL', 127);
    const wet = await sound('held-only');
    expect(wet.peak, 'recorded grains remain audible after the live note ends').toBeGreaterThan(0.005);
    held = await status();
    expect(held[8], 'transport stop preserves the completed phrase').toBe(committed);
    expect(float(held[23]!), 'router reached the PRISM endpoint').toBeGreaterThan(0.99);

    // RATE OFF intentionally removes the wet return after current grains end.
    // A new selected note must now be silent, while an unselected track still
    // reaches the native output. This tests the real selective dry path.
    await tap(session, 'DOWN', 80);
    await set(7, 'ENC B', 0, true);
    await advance(session, 650);
    const selected = await sound('selected-at-prism', 1);
    const unselected = await sound('unselected-at-prism', 2);
    expect(unselected.peak).toBeGreaterThan(0.01);
    expect(selected.rms, 'selected dry source is removed at full PRISM').toBeLessThan(unselected.rms * 0.001);
    await advance(session, 2200);
    await set(20, 'LEVEL', 0);
    const restored = await sound('selected-live-restored', 1);
    expect(restored.peak).toBeGreaterThan(0.01);
    const chain = await rows(session, plan, context.slots);
    expect(chain[0]![17], 'production FX guard stayed healthy').toBe(0);
    record(artifacts, 'routing-audio', { live, wet, selected, unselected, restored, held, chain });

    // A numbered cue armed while stopped must survive the qualified START
    // epoch, then record exactly one step. Retain the running-NOW proof above.
    await tap(session, 'UP', 80);
    await set(3, 'ENC D', 1);
    const beforeStart = await status();
    let armedClock: number[] = [];
    const cuedAudio = await sound(
      'stopped-cue-start',
      1,
      async () => {
        await set(0, 'ENC A', 1, true);
        const armed = await status();
        armedClock = await control();
        expect(armedClock[13]! & 2, 'cue is armed with transport stopped').toBe(0);
        expect(armed[3], 'numbered cue waits in ARMED').toBe(3);
        expect(armed[20], 'waiting cue keeps REC enabled').toBe(1);
        expect(await value(0)).toBe(1);
        await tap(session, 'PLAY', 80);
      },
      1000,
    );
    const cued = await status();
    const startedClock = await control();
    expect(cuedAudio.peak).toBeGreaterThan(0.01);
    expect(startedClock[13]! & 16, 'provider retains the qualified START epoch reason').toBe(16);
    expect(startedClock[12], 'transport start advances exactly one timeline epoch').toBe((armedClock[12]! + 1) >>> 0);
    expect(cued[4], 'stopped cue completes without timing error').toBe(0);
    expect(cued[19], 'a new phrase committed after START').toBe((beforeStart[19]! + 1) >>> 0);
    expect(cued[8]).toBeGreaterThanOrEqual(Math.floor(expectedFrames) - 32);
    expect(cued[8]).toBeLessThanOrEqual(Math.ceil(expectedFrames) + 32);
    expect(cued[11]).toBe(1);
    expect(cued[20]).toBe(0);
    expect(await value(0)).toBe(0);
    record(artifacts, 'stopped-cue-start', { armedClock, startedClock, beforeStart, cued, expectedFrames, cuedAudio });
    await tap(session, 'STOP', 80);
    await bounded(() => session.expectNoFault('PRISM native capture and selective fade'));
  }, 300_000);
});
