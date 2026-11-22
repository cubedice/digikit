import { portAnchorAddress } from '@digi/regression/runtime/device-data.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { waitEmulated } from '@digi/regression/runtime/port-compat.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';

describeOn('dt2', 'stock audio helper group', () => {
  const handle = withSession({ ...liveOptions('dt2', { fresh: true }), dsp: 'sharc', restart: false });
  test('leases an empty helper, keeps it through PLAY and re-admits after switching to WERP', { timeout: 240_000 }, async () => {
    const { session } = handle;
    const image = await prepareRuntimePlanModulesOffline(
      session.options, session.runDir, session.runDir,
      ['DKRT_BOOT', 'tk_native_audio_active', 'pp_position', 'tk_sequence_audio_hits'],
      { modules: ['core/track-keyboard', 'core/instrument-play', 'core/digikit-menu', 'core/health'] },
    );
    const word = (name: string, offset = 0) => session.client.memPeek(planSymbol(image.plan, name) + offset);
    const mask = async (name: string) => ((await word(`.sec..bss.${name}`)) ?? 0) >>> 16;
    await session.client.firmwareLoad({
      syx: image.syx, base_sha256: image.plan.base.syx_sha256, start: 'auto',
      force: true, auto_no: true, card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    await expectRuntimeUi(session, image.plan, session.runDir);
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
    await chord();
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
    await pollEmulated(session, async () => ((await word('.sec..bss.machine')) ?? 0) >>> 24,
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
