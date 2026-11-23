// device-literals: allow (uses the DTII stock voice records)
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { dspWords } from '@digi/regression/runtime/fx-live.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { freeClock, waitClock } from '@digi/test-support';
import { expect, test } from 'vitest';

const OWNER = 1;
const TRIG_9 = 33;
const STOCK_VOICE_FLAGS = 0x241484;
const STOCK_TRACK_STRIDE = 0x3b0;

function trackBits(mask: number): number[] {
  return Array.from({ length: 16 }, (_, track) => track).filter((track) => mask & (1 << track));
}

describeOn('dt2', 'automatic stock audio helper pool', () => {
  const handle = withSession({ ...liveOptions('dt2', { fresh: true }), dsp: 'sharc', restart: false });

  test('borrows only for the notes being played and returns empty tracks after release', { timeout: 180_000 }, async () => {
    const { session } = handle;
    const image = await prepareRuntimePlanModulesOffline(
      session.options, session.runDir, session.runDir,
      ['DKRT_BOOT', 'tk_native_audio_active', 'pp_position', 'tk_capture_committed', 'tk_panel_claim_hits'],
      { modules: ['core/track-keyboard', 'core/instrument-play', 'core/digikit-menu', 'core/health'] },
    );
    const word = async (symbol: string, offset = 0) => (await session.client.memPeek(planSymbol(image.plan, symbol) + offset)) ?? 0;
    const mask = async (name: string, offset = 0) => (await word('.sec..bss.' + name, offset)) >>> 16;
    await session.client.firmwareLoad({
      syx: image.syx, base_sha256: image.plan.base.syx_sha256, start: 'auto',
      force: true, auto_no: true, card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    await expectRuntimeUi(session, image.plan, session.runDir);
    const { edge, tap, turn } = acknowledgedPanel(session);
    const selectTrack = async (track: number) => {
      await edge('TRK', true);
      try {
        await tap('TRIG ' + (track + 1));
      } finally {
        await edge('TRK', false);
      }
    };
    await tap('NO'); // Dismiss the stock FILE SYSTEM OK acknowledgement.
    await selectTrack(0);
    const initialEligible = await pollEmulated(session, () => mask('eligible'),
      (value) => trackBits(value & ~OWNER).length >= 3, {
        emulatedMs: 4000, message: 'stock card has three empty tracks to test',
      });
    const [protectedTrack] = trackBits(initialEligible & ~OWNER);
    const protectedMask = 1 << protectedTrack!;
    await selectTrack(protectedTrack!);
    await tap('RECORD');
    await tap('TRIG 1');
    await tap('RECORD');
    await selectTrack(0);
    await pollEmulated(session, () => mask('eligible'),
      (value) => !(value & protectedMask) && trackBits(value & ~OWNER).length >= 2, {
        emulatedMs: 4000, message: 'placing a sequencer trig removes that track from the available pool',
      });

    await tap('SRC');
    for (let page = 0; page < 4; page++) await tap('PAGE TRIG');
    await turn(1, 12); // POLY
    await turn(2, 4); // MAJ, generated from one pressed key.
    await tap('KEYBOARD');

    const state = async () => ({
      assigned: await mask('pool'),
      draining: await mask('pool', 32),
      preparing: await mask('preparing'),
      reserved: await mask('reserved'),
      active: await word('tk_native_audio_active'),
      quiet: (await word('.sec..bss.reply_word')) & 0xffff,
      eligible: await mask('eligible'),
      empty: await mask('empty_tracks'),
      wanted: await mask('wanted'),
    });
    const panelChord = (down: boolean) => session.client.inputButton(TRIG_9, down);
    const soundingState = async () => {
      const current = await state();
      const destinations = OWNER | current.assigned;
      let dspActive = 0;
      for (const track of trackBits(current.active & destinations)) {
        // Each DTII audio track has two alternating sample voice records.
        const [primary] = await dspWords(session, STOCK_VOICE_FLAGS + track * STOCK_TRACK_STRIDE, 1);
        const [alternate] = await dspWords(session, STOCK_VOICE_FLAGS + track * STOCK_TRACK_STRIDE + 0x1d8, 1);
        if (((primary ?? 0) | (alternate ?? 0)) & 1) dspActive |= 1 << track;
      }
      return { ...current, dspActive };
    };
    const playAndRelease = async (
      label: string,
      key = panelChord,
      afterRelease?: (helpers: number) => Promise<void>,
    ) => {
      const idle = await state();
      expect(idle.assigned | idle.draining | idle.reserved | idle.active, label + ' has no idle leases').toBe(0);
      const states: string[] = [];
      let helpers = 0;
      await key(true);
      try {
        const sounding = await pollEmulated(session, async () => {
          const current = await soundingState();
          const summary = Object.entries(current).map(([key, value]) => key + '=' + value.toString(16)).join(' ');
          if (states.at(-1) !== summary) states.push(summary);
          return current;
        }, (value) => trackBits(value.assigned).length === 2 && value.dspActive === (OWNER | value.assigned), {
          emulatedMs: 1800,
          intervalMs: 5,
          message: () => label + ' sounds a chord on three actual DSP voice destinations\n'
            + 'Before onset: ' + JSON.stringify(idle) + ', protected mask: ' + protectedMask.toString(16)
            + '\nObserved masks in hex:\n' + states.join('\n'),
        });
        helpers = sounding.assigned;
        expect(sounding.reserved, label + ' only reserves the helpers required by the triad').toBe(helpers);
        expect((sounding.reserved | sounding.active) & protectedMask, label + ' leaves the populated track independent').toBe(0);
      } finally {
        await key(false);
      }
      await afterRelease?.(helpers);
      const released = await pollEmulated(session, state, (current) => {
        expect((current.assigned | current.draining | current.reserved) & protectedMask).toBe(0);
        return (current.assigned | current.draining | current.reserved | current.active) === 0
          && (current.quiet & helpers) === helpers;
      }, {
        emulatedMs: 7000, intervalMs: 10, message: label + ' returns all helpers after the DSP reports them quiet',
      });
      console.info(label + ' AUTO pool', {
        eligible: idle.eligible.toString(16),
        quietBefore: idle.quiet.toString(16),
        protected: protectedMask.toString(16),
        sounding: (OWNER | helpers).toString(16),
        quietAfter: released.quiet.toString(16),
      });
    };

    await playAndRelease('Stopped transport', panelChord, async (helpers) => {
      const destinations = OWNER | helpers;
      await pollEmulated(session, () => word('tk_native_audio_active'), (active) => !(active & destinations), {
        emulatedMs: 300, intervalMs: 2, message: 'first chord released its generated notes',
      });
      expect((await word('.sec..bss.reply_word')) & helpers, 'the helper sample tails are still audible').toBe(0);

      const nextRoot = TRIG_9 + 1;
      await session.client.inputButton(nextRoot, true);
      try {
        await pollEmulated(session, soundingState,
          (current) => current.assigned === helpers && current.dspActive === destinations, {
            emulatedMs: 450, intervalMs: 4, message: 'second root sounds a full triad on the same helper lanes',
          });
      } finally {
        await session.client.inputButton(nextRoot, false);
      }
    });
    await tap('PLAY');
    try {
      await pollEmulated(session, () => word('pp_position', 20), (flags) => !!(flags & 2), {
        emulatedMs: 2000, message: 'stock transport is running',
      });
      await waitClock(freeClock(session), 200); // Let native transport and FX finish their initial kit update.
      await playAndRelease('Running transport');
    } finally {
      await tap('STOP');
    }

    // With both stopped and running audition confirmed, exercise the same
    // generated triad while the native recorder writes the USB performance.
    const writeSetting = async (addr: number, value: number) => {
      const bytes = Buffer.alloc(4);
      bytes.writeUInt32BE(value);
      await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
    };
    await writeSetting(0x429368ca, 3); // Native receive port mask permits USB.
    await writeSetting(0x42936872, 0); // Auto Channel 1 addresses the selected track.
    await writeSetting(0x429368ee, 1); // Receive Notes.
    const checkLiveRecording = async (label: string, key: typeof panelChord, openingMs: number) => {
      const captureBefore = await word('tk_capture_committed');
      await edge('RECORD', true);
      try {
        await tap('PLAY');
      } finally {
        await edge('RECORD', false);
      }
      try {
        await pollEmulated(session, () => word('pp_position', 20), (flags) => !!(flags & 2), {
          emulatedMs: 2000, message: label + ' started the transport',
        });
        await waitClock(freeClock(session), openingMs);
        await pollEmulated(session, state,
          ({ assigned, draining, reserved, active }) => !(assigned | draining | reserved | active), {
            emulatedMs: 2500, intervalMs: 10, message: label + ' starts with the previous notes released',
          });
        await playAndRelease(label, key, async () => {
          await pollEmulated(session, () => word('tk_capture_committed'), (count) => count > captureBefore, {
            emulatedMs: 1200, intervalMs: 10, message: label + ' committed the auditioned triad seed',
          });
          await tap('STOP');
        });
      } finally {
        await tap('STOP');
      }
    };
    const usbChord = (down: boolean) =>
      session.client.call('midi.usb.in', { bytes: [down ? 0x90 : 0x80, 60, down ? 100 : 0] });
    await checkLiveRecording('USB live recording', usbChord, 200);

    // The stock Live Rec banner intercepts the first panel presses, unlike USB.
    const panelClaimBefore = await word('tk_panel_claim_hits');
    await checkLiveRecording('Panel live recording', panelChord, 1700);
    expect(await word('tk_panel_claim_hits'), 'the panel key entered the shared audition').toBe(panelClaimBefore + 1);
    await session.expectNoFault('AUTO stock voice borrowing while playing a chord');
  });
});
