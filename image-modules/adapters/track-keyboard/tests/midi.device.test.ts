import { describeOn } from '@digi/regression/runtime/devices.ts';
import { liveOptions } from '@digi/regression/runtime/live-options.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';
import { trackKeyboardCard, trackKeyboardSymbols } from './track-card.ts';

describeOn('dt2', 'shared track keyboard MIDI ingress proof', () => {
  const handle = withSession({ ...trackKeyboardCard(liveOptions('dt2', { fresh: true })), dsp: 'sharc', restart: false });
  test(
    'keeps USB, DIN and stock fan-out ownership separate',
    { timeout: 180_000 },
    async () => {
      const { session } = handle;
      const image = await prepareRuntimePlanModulesOffline(
        session.options,
        session.runDir,
        session.runDir,
        ['DKRT_BOOT'],
        { modules: ['core/track-keyboard', 'core/health'] },
      );
      await session.client.firmwareLoad({
        syx: image.syx,
        base_sha256: image.plan.base.syx_sha256,
        start: 'auto',
        force: true,
        auto_no: true,
        card: session.cardPath,
      });
      await session.client.call('run.mode', { mode: 'free' });
      await expectRuntimeUi(session, image.plan, session.runDir);
      const sym = await trackKeyboardSymbols(session, image.plan);
      const { edge, tap, turn } = acknowledgedPanel(session);
      const selectTrack = async (track: number) => {
        await edge('TRK', true);
        try {
          await tap(`TRIG ${track}`);
        } finally {
          await edge('TRK', false);
        }
      };
      const write = (addr: number, value: number) => {
        const bytes = Buffer.alloc(4);
        bytes.writeUInt32BE(value);
        return session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
      };
      const control = (port: 'usb' | 'din', bytes: number[]) =>
        session.client.call(port === 'usb' ? 'midi.usb.in' : 'midi.in', { bytes });
      const held = (track: number) => session.client.memPeek(0x44e086ec + (track - 1) * 4);
      const generated = async (track: number) => {
        const mask = await session.client.memPeek(sym('tk_native_audio_active'));
        return ((mask ?? 0) >> (track - 1)) & 1;
      };
      const expectHeld = (count: () => Promise<number | null>, n: number, message: string) =>
        pollEmulated(session, count, (actual) => actual === n, {
          emulatedMs: 3000,
          intervalMs: 20,
          message,
        });

      await selectTrack(1);
      for (let page = 0; page < 4; ++page) await tap(1);
      await turn(1, 12); // KEYS A, POLY.
      await write(0x429368ca, 3); // Native receive port mask, USB and DIN.
      await write(0x42936872, 0); // Auto Channel 1.
      await write(0x429368ee, 1); // Native Receive Notes flag.

      await control('usb', [0x90, 60, 100]);
      await expectHeld(() => generated(1), 1, 'USB Auto Channel drives the shared backend');
      expect(await held(1)).toBe(0);
      await control('din', [0x90, 60, 85]);
      await control('usb', [0x90, 60, 0]); // Zero-velocity note-on is release.
      await session.wait(120);
      expect(await generated(1)).toBe(1); // DIN still owns the shared pitch.
      await control('din', [0x80, 60, 0]);
      await expectHeld(() => generated(1), 0, 'last owner releases the shared pitch');

      await control('usb', [0x90, 60, 100]);
      await control('din', [0x90, 60, 85]);
      await control('usb', [0xb0, 64, 127]);
      await control('usb', [0x90, 60, 0]);
      await session.wait(120);
      expect(await generated(1)).toBe(1);
      await selectTrack(2);
      await control('din', [0x80, 60, 0]);
      await session.wait(120);
      expect(await generated(1)).toBe(1); // USB sustain outlasts the DIN release.
      expect(await held(2)).toBe(0);
      await control('usb', [0xb0, 64, 0]);
      await expectHeld(() => generated(1), 0, 'pedal release follows its owner after track selection');
      expect(await held(2)).toBe(0);

      // Both tracks listen to an ordinary input channel. The shared route
      // plays one generated note while the other destination stays native.
      await write(0x42936876, 1);
      await write(0x4293687a, 1);
      await control('din', [0x91, 67, 100]);
      await expectHeld(() => generated(1), 1, 'mixed channel listeners include the shared backend');
      await expectHeld(() => held(2), 1, 'mixed channel listeners retain the stock backend');
      await session.wait(120);
      expect(await generated(1)).toBe(1);
      expect(await held(1)).toBe(0);
      await control('din', [0x81, 67, 0]);
      await expectHeld(() => generated(1), 0, 'mixed release reaches the shared backend');
      await expectHeld(() => held(2), 0, 'mixed release reaches the stock backend');

      await selectTrack(1);
      await control('usb', [0x90, 64, 100]);
      await expectHeld(() => generated(1), 1, 'USB owner sounds before receive disable');
      await write(0x429368ca, 1);
      await expectHeld(() => generated(1), 0, 'disabling receive releases its owned note');
      await write(0x429368ca, 3);
      await session.wait(50);
      await control('usb', [0x90, 64, 100]);
      await expectHeld(() => generated(1), 1, 'fresh key works after receive is restored');
      await control('usb', [0x80, 64, 0]);
      await expectHeld(() => generated(1), 0, 'restored key has a matching release');

      const commits = () => session.client.memPeek(sym('tk_capture_committed'));
      const before = (await commits()) ?? 0;
      await edge('RECORD', true);
      try {
        await tap('PLAY');
      } finally {
        await edge('RECORD', false);
      }
      try {
        await session.wait(1500);
        await control('usb', [0x90, 60, 100, 0x90, 64, 95, 0x90, 67, 90]);
        await pollEmulated(session, commits, (n) => (n ?? 0) > before, {
          emulatedMs: 5000,
          intervalMs: 50,
          message: 'stock live recorder commits a USB performance recipe',
        });
        await control('usb', [0x80, 60, 0, 0x80, 64, 0, 0x80, 67, 0]);
      } finally {
        await tap('STOP');
      }
      await session.expectNoFault('shared USB/DIN note ownership');
    },
  );
});
