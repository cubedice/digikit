import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { test } from 'vitest';

describeOn('dt2', 'shared keyboard track clipboard', () => {
  const handle = withSession({ ...liveOptions('dt2', { fresh: true }), dsp: 'sharc', restart: false });
  test(
    'copies, clears, and restores one track through the native clipboard',
    { timeout: 180_000 },
    async () => {
      const { session } = handle;
      const { syx, plan } = await prepareRuntimePlanModulesOffline(
        session.options,
        session.runDir,
        session.runDir,
        ['DKRT_BOOT', 'tk_storage_info', 'pattern_slots'],
        { modules: ['core/track-keyboard', 'core/instrument-play', 'core/digikit-menu', 'core/health'] },
      );
      await session.client.firmwareLoad({
        syx,
        base_sha256: plan.base.syx_sha256,
        start: 'auto',
        force: true,
        auto_no: true,
        card: session.cardPath,
      });
      await session.client.call('run.mode', { mode: 'free' });
      await expectRuntimeUi(session, plan, session.runDir);

      const { tap, turn, edge } = acknowledgedPanel(session);
      const modified = async (modifier: string, key: string) => {
        await edge(modifier, true);
        try {
          await tap(key);
        } finally {
          await edge(modifier, false);
        }
      };
      const arp = async (track: number) => {
        const pattern = await session.client.memPeek(planSymbol(plan, 'pattern_slots'));
        if (!pattern) return 0;
        const word = await session.client.memPeek(pattern + 56 * (track - 1));
        if (word === null) throw new Error('Unreadable A01 keyboard data');
        return word >>> 24; // TkTrackConfig.params.arp, with one 56-byte config per track.
      };
      const expectArp = (track: number, enabled: boolean, message: string) =>
        pollEmulated(session, () => arp(track), (n) => n === Number(enabled), { emulatedMs: 5000, message });
      const copies = () => session.client.memPeek(planSymbol(plan, 'tk_storage_info') + 8);

      await modified('TRK', 'TRIG 2');
      for (let page = 0; page < 3; ++page) await tap(1);
      await turn(1, 12);
      await expectArp(1, false, 'source edits leave track 1 alone');
      await expectArp(2, true, 'source track 2 ARP is enabled');
      // Give the source one native trig as well as the keyboard attachment.
      // In grid recording, FUNC+RECORD/STOP/PLAY apply to the track sequence.
      await tap('RECORD');
      await tap('TRIG 3');
      const initialCopies = (await copies()) ?? 0;
      await modified('FUNC', 'RECORD');
      await pollEmulated(session, copies, (n) => (n ?? 0) > initialCopies, {
        emulatedMs: 5000,
        message: 'copy captured track 2 keyboard settings',
      });

      await modified('TRK', 'TRIG 1');
      await modified('FUNC', 'STOP');
      await expectArp(1, true, 'paste sets the destination track ARP');
      await modified('FUNC', 'STOP');
      await expectArp(1, false, 'undo paste restores the destination settings');
      await modified('FUNC', 'STOP');
      await expectArp(1, true, 'the clipboard remains available after undo');

      await modified('FUNC', 'PLAY');
      await expectArp(1, false, 'clear removes the same track keyboard attachment');
      await expectArp(2, true, 'clear leaves neighboring track settings');
      await modified('FUNC', 'PLAY');
      await expectArp(1, true, 'undo clear restores the track keyboard attachment');
      await session.expectNoFault('track keyboard clipboard');
    },
  );
});
