import path from 'node:path';
import { portAnchorAddress } from '@digi/regression/runtime/device-data.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { dspWords, peak, readFloatWav } from '@digi/regression/runtime/fx-live.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { waitEmulated } from '@digi/regression/runtime/port-compat.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';
import { trackKeyboardCard, trackKeyboardSymbols } from './track-card.ts';

const AUDIO_RATE = 48_000;
const WINDOW = 4096;

/** Measure one channel in a settled window with the same normalization across captures. */
function tone(pcm: Float32Array, frequency: number, channel: 0 | 1): number {
  expect(pcm.length).toBeGreaterThanOrEqual(2 * WINDOW);
  const offset = Math.floor((pcm.length / 2 - WINDOW) / 2);
  let real = 0;
  let imaginary = 0;
  for (let i = 0; i < WINDOW; i++) {
    const window = (1 - Math.cos((2 * Math.PI * i) / (WINDOW - 1))) / 2;
    const value = pcm[2 * (i + offset) + channel]! * window;
    const phase = (2 * Math.PI * frequency * i) / AUDIO_RATE;
    real += value * Math.cos(phase);
    imaginary += value * Math.sin(phase);
  }
  return Math.hypot(real, imaginary) / WINDOW;
}

function fundamental(pcm: Float32Array): number {
  // Track 1 of the stock test card is a one-second 220 Hz sine.
  let frequency = 0;
  let maximum = 0;
  for (let probe = 100; probe <= 550; probe += 2) {
    const level = tone(pcm, probe, 0);
    if (level > maximum) {
      maximum = level;
      frequency = probe;
    }
  }
  expect(maximum, 'stock sample is audible in the held-note reference').toBeGreaterThan(0.001);
  return frequency;
}

describeOn('dt2', 'stock audio helper group', () => {
  const handle = withSession({ ...trackKeyboardCard(liveOptions('dt2', { fresh: true })), dsp: 'sharc', restart: false });
  test('leases an empty helper, follows the owner controls, and re-admits after switching to WERP', { timeout: 240_000 }, async () => {
    const { session } = handle;
    const image = await prepareRuntimePlanModulesOffline(
      session.options, session.runDir, session.runDir,
      ['DKRT_BOOT', 'pp_position'],
      { modules: ['core/track-keyboard', 'core/digikit-menu', 'core/health'] },
    );
    let sym = (name: string) => planSymbol(image.plan, name);
    const word = (name: string, offset = 0) => session.client.memPeek(sym(name) + offset);
    const mask = async (name: string) => ((await word(name)) ?? 0) >>> 16;
    await session.client.firmwareLoad({
      syx: image.syx, base_sha256: image.plan.base.syx_sha256, start: 'auto',
      force: true, auto_no: true, card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    await expectRuntimeUi(session, image.plan, session.runDir);
    sym = await trackKeyboardSymbols(session, image.plan);
    const { edge, tap, turn } = acknowledgedPanel(session);
    await tap('NO'); // Close the stock FILE SYSTEM OK acknowledgement before sending panel gestures.
    await edge('TRK', true);
    try {
      await tap('TRIG 1');
    } finally {
      await edge('TRK', false);
    }
    const candidates = (await mask('eligible')) & ~1;
    expect(candidates).toBeGreaterThan(0);
    const helper = 31 - Math.clz32(candidates & -candidates);
    const helperMask = 1 << helper;
    await tap('SRC');
    for (let page = 0; page < 4; page++) await tap('PAGE TRIG');
    await turn(1, 12);
    const led = async (track: number) => {
      const { leds } = await session.leds();
      return parseInt(leds.slice(2 * (25 + track), 2 * (26 + track)), 16);
    };
    const selectHelper = async () => {
      await tap('PUSH H');
      await pollEmulated(session, () => led(0), (value) => value === 0x41, {
        emulatedMs: 2000, message: 'the owner is blue in the native group picker',
      }).catch(async (error) => {
        await captureRemoteOled(session, 'pool-owner-led-failed');
        throw error;
      });
      await tap(`TRIG ${helper + 1}`);
      await pollEmulated(session, () => led(helper), (value) => value === 0x71, {
        emulatedMs: 2000, message: 'the staged helper lights white with no lease yet',
      });
      expect(await mask('reserved')).toBe(0);
    };
    await selectHelper();
    await tap('NO');
    expect(await mask('reserved')).toBe(0);
    await pollEmulated(session, () => led(0), (value) => value !== 0x41, {
      emulatedMs: 2000, message: 'cancel returns LED ownership to the native panel',
    });
    await selectHelper();
    await tap('YES'); // APPLY GROUP
    await pollEmulated(session, () => mask('reserved'), (value) => value === helperMask, {
      emulatedMs: 4000, message: 'idle helper is admitted only after applying the draft',
    });
    await tap('KEYBOARD');
    const both = (value: number | null) => ((value ?? 0) & (helperMask | 1)) === (helperMask | 1);
    const chord = async (verifySound?: () => Promise<void>) => {
      await edge('TRIG 9', true);
      await edge('TRIG 11', true);
      try {
        await pollEmulated(session, () => word('tk_native_audio_active'), both, {
          emulatedMs: 4000, message: 'same owner sends two pitches to distinct physical voices',
        });
        await verifySound?.();
      } finally {
        await edge('TRIG 11', false);
        await edge('TRIG 9', false);
      }
    };
    const rendered = async (label: string, keys: string[], duration = 150) => {
      await waitEmulated(session, 1200); // The test sample lasts one second at its original pitch.
      const file = path.join(session.runDir, `pool-${label}.wav`);
      for (const key of keys) await edge(key, true);
      try {
        const active = keys.length === 2 ? both : (value: number | null) => !!((value ?? 0) & 1);
        await pollEmulated(session, () => word('tk_native_audio_active'), active, {
          emulatedMs: 2000, message: `${label} owns its expected physical destinations`,
        });
        await session.client.call('pcm.tap', { action: 'capture', path: file });
        try {
          await waitEmulated(session, duration);
        } finally {
          await session.client.call('pcm.tap', { action: 'capture_stop' });
        }
      } finally {
        for (const key of keys.toReversed()) await edge(key, false);
      }
      return readFloatWav(file);
    };
    const keys = ['TRIG 9', 'TRIG 11'];
    const frequencies = [
      fundamental(await rendered('owner-note-1', keys.slice(0, 1))),
      fundamental(await rendered('owner-note-2', keys.slice(1))),
    ];
    expect(Math.abs(frequencies[1]! - frequencies[0]!), 'two distinct pitches').toBeGreaterThan(12);
    const dry = await rendered('center', keys);
    const stereo = (pcm: Float32Array, frequency: number) => [tone(pcm, frequency, 0), tone(pcm, frequency, 1)] as const;
    const sweep = async (encoder: number, direction: 1 | -1) => {
      for (let i = 0; i < 6; i++) await turn(encoder, 28 * direction);
    };
    await tap('AMP');
    await sweep(7, 1); // AMP G PAN.
    const pan = await rendered('right', keys);
    await tap('FLTR');
    // The card's two pitches are near the bottom of the audio range. With
    // FREQ open, the MULTI-MODE lowpass/highpass endpoints give a clear test.
    await sweep(5, 1); // FLTR E FREQ.
    await sweep(7, 1); // FLTR G TYPE, from lowpass to highpass.
    await captureRemoteOled(session, 'pool-highpass');
    const filtered = await rendered('highpass', keys);
    await sweep(7, -1);
    const open = await rendered('filter-open', keys);
    await tap('FX');
    await sweep(2, 1); // FX B OVERDRIVE.
    const driven = await rendered('overdrive', keys);
    for (const frequency of frequencies) {
      const [dryLeft, dryRight] = stereo(dry, frequency);
      const [panLeft, panRight] = stereo(pan, frequency);
      const [, filteredRight] = stereo(filtered, frequency);
      const [, openRight] = stereo(open, frequency);
      const [, drivenRight] = stereo(driven, frequency);
      const cleanRatio = tone(open, frequency * 3, 1) / openRight;
      const drivenRatio = tone(driven, frequency * 3, 1) / drivenRight;
      console.info('POOL owner-control PCM', {
        frequency, dryLeft, dryRight, panLeft, panRight, filteredRight, openRight, cleanRatio, drivenRatio,
      });
      expect(Math.min(dryLeft, dryRight), `${frequency} Hz is audible in both channels`).toBeGreaterThan(0.001);
      expect(panRight, `${frequency} Hz follows the owner's right PAN`).toBeGreaterThan(3 * panLeft);
      expect(panRight, `${frequency} Hz stays present after panning`).toBeGreaterThan(dryRight / 2);
      expect(filteredRight, `${frequency} Hz follows the owner's filter`).toBeLessThan(openRight / 2);
      expect(drivenRatio, `${frequency} Hz acquires overdrive harmonics`).toBeGreaterThan(cleanRatio * 3 + 0.001);
    }
    await sweep(2, -1);
    await waitEmulated(session, 1200);
    await sweep(6, 1); // FX F REV.

    // Use the original routing menus. The helper is still allowed in the
    // master when we exclude the owner, so a duplicate dry voice would sound.
    const routes = async (actions: ('main' | 'sends')[]) => {
      await tap('SETTINGS');
      for (let i = 0; i < 9; i++) await tap('UP');
      for (let i = 0; i < 5; i++) await tap('DOWN');
      await tap('YES'); // AUDIO ROUTING.
      for (const route of actions) {
        await tap('UP');
        await tap('UP');
        if (route === 'sends') await tap('DOWN');
        await tap('YES'); // TO MAIN or TO SEND FX.
        await tap('TRIG 1');
        await tap('NO');
      }
      await tap('NO');
      await tap('NO');
    };
    const mainMask = async () => (await dspWords(session, 0x252728, 1))[0]!;
    expect((await mainMask()) & (helperMask | 1)).toBe(0);
    await routes(['main', 'sends']);
    await pollEmulated(session, mainMask, (value) => (value & (helperMask | 1)) === 1, {
      emulatedMs: 2000, message: 'the owner alone is excluded from the native main output',
    });
    const excluded = await rendered('owner-excluded', keys, 440);
    expect(peak(excluded), 'owner exclusion gates dry and FX for the entire group').toBeLessThan(1e-5);
    await routes(['sends']);
    const wet = await rendered('owner-reverb-only', keys, 440);
    expect(peak(wet), 'the owner FX route carries the group even with its dry route excluded').toBeGreaterThan(1e-3);
    await routes(['main']);
    await pollEmulated(session, mainMask, (value) => (value & (helperMask | 1)) === 0, {
      emulatedMs: 2000, message: 'the owner is included in the native main output again',
    });
    await tap('FX');
    await sweep(6, -1);
    await tap('AMP');
    await sweep(7, -1);
    await tap('SRC');
    await tap('PLAY');
    await pollEmulated(session, () => word('pp_position', 20), (flags) => !!((flags ?? 0) & 2), {
      emulatedMs: 2000, message: 'native pattern is running',
    });
    expect((await mask('reserved')) & helperMask).toBe(helperMask);
    await chord();
    await tap('STOP');
    await tap('KEYBOARD');

    // The native machine picker initializes the owner's WERP Sound. Keep the
    // stored pool request and wait for its replacement lease to settle.
    await edge('FUNC', true);
    try {
      await tap('SRC');
    } finally {
      await edge('FUNC', false);
    }
    await tap('DOWN');
    await tap('YES');
    await tap('YES');
    // The pool sees the kit assignment before an audition updates the DSP
    // frame's active Sound. Observe each at the appropriate point.
    await pollEmulated(session, async () => ((await word('machine')) ?? 0) >>> 24,
      (type) => type === 1, {
        emulatedMs: 4000, message: 'owner kit assigns native WERP',
      });
    const soundingMachine = async (track: number) => {
      const at = 0x800053dc + 2 * track;
      const pair = (await session.client.memPeek((at & ~3) >>> 0)) ?? 0;
      return (pair >>> (track % 2 === 0 ? 16 : 0)) & 0xffff;
    };
    await pollEmulated(session, () => mask('pool'), (value) => value === helperMask, {
      emulatedMs: 4000, message: 'same request admits a WERP group after the old one drains',
    });
    await tap('KEYBOARD');
    await chord(async () => {
      await pollEmulated(
        session, async () => Promise.all([soundingMachine(0), soundingMachine(helper)]),
        ([ownerType, helperType]) => ownerType === 1 && helperType === 1, {
          emulatedMs: 4000, message: 'both active voices receive the ownerâ€™s WERP Sound',
        },
      );
    });
    await tap('KEYBOARD');
    await tap('RECORD');
    await tap('TRIG 1');
    await tap('RECORD');
    await pollEmulated(session, () => mask('pool'), (value) => value === helperMask, {
      emulatedMs: 4000, message: 'an owner trig does not make the empty helper ineligible',
    });
    const hits = async () => (await word('tk_sequence_audio_hits')) ?? 0;
    const beforePlay = await hits();
    await tap('PLAY');
    await pollEmulated(session, hits, (count) => count > beforePlay, {
      emulatedMs: 4000, message: 'stock sequencer triggers the shared audio lane',
    });
    await edge('FUNC', true);
    try {
      await tap('TRIG 1');
    } finally {
      await edge('FUNC', false);
    }
    const muteLiteral = portAnchorAddress('dt2', '1.17', 'cf.track_sequence.site.audio_mute_lit');
    const muteAddress = await session.client.memPeek(muteLiteral);
    expect(muteAddress).toBeGreaterThan(0);
    const nativeMute = async () => ((await session.client.memPeek(muteAddress!)) ?? 0) & 1;
    await pollEmulated(session, nativeMute, Boolean, {
      emulatedMs: 4000, message: 'the native owner mute is active',
    });
    await tap('KEYBOARD');
    const beforeMuted = await hits();
    await chord();
    await waitEmulated(session, 2500);
    expect(await hits()).toBe(beforeMuted);
    expect(await nativeMute()).toBe(1);
  });
});
