import { expect, test } from 'vitest';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { trackKeyboardCard, trackKeyboardSymbols } from './track-card.ts';

describeOn('dt2', 'individual shared keyboard trigs', () => {
  const handle = withSession({ ...trackKeyboardCard(liveOptions('dt2', { fresh: true })), dsp: 'sharc', restart: false });
  test(
    'moves, clears, and undoes an individual trig attachment',
    { timeout: 180_000 },
    async () => {
      const { session } = handle;
      const { syx, plan } = await prepareRuntimePlanModulesOffline(
        session.options,
        session.runDir,
        session.runDir,
        ['DKRT_BOOT', 'pattern_slots'],
        { modules: ['core/track-keyboard', 'core/digikit-menu', 'core/health'] },
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
      const sym = await trackKeyboardSymbols(session, plan);
      const { edge, tap, turn } = acknowledgedPanel(session);
      const a01 = await session.client.memPeek(0x4316501c);
      if (!a01) throw new Error('A01 is not selected');
      // Set the stock pattern and track lengths for the wrap case. The
      // clipboard operations and keyboard settings still use the panel.
      for (const offset of [0x1db74, 0x48c])
        await session.client.call('mem.cf.write', { addr: a01 + offset, hex: '0010' });

      const held = async (key: string, action: () => Promise<unknown>) => {
        await edge(key, true);
        try {
          await action();
        } finally {
          await edge(key, false);
        }
      };
      const info = (word: number) => session.client.memPeek(sym('tk_step_clip_info') + word * 4);
      const waitCount = (word: number, count: number, message: string) =>
        pollEmulated(session, () => info(word), (actual) => actual === count, { emulatedMs: 4000, message });
      const nativeTrig = async (step: number) => {
        const flags = await session.client.memPeek(a01 + 2 * (step - 1));
        if (flags === null) throw new Error('Unreadable native trig');
        return (flags >>> 16) & 1;
      };
      const masks = async () => {
        const pattern = await session.client.memPeek(sym('pattern_slots'));
        if (!pattern) throw new Error('A01 did not attach shared keyboard data');
        const rows = await Promise.all(
          Array.from({ length: 4 }, (_, i) => session.client.memPeek(pattern + 0x380 + 62 * i)),
        );
        const out = new Map<number, number>();
        // This fresh fixture uses at most two attachment slots.
        for (const row of rows) {
          if (row === null) throw new Error('Unreadable A01 attachment');
          const index = row >>> 16;
          if (index) out.set(index, row & 0xffff);
        }
        return out;
      };

      await held('TRK', () => tap('TRIG 1'));
      for (let page = 0; page < 3; ++page) await tap(1);
      await tap('RECORD');
      await tap('TRIG 1');
      await tap('TRIG 3');
      await held('TRIG 1', () => turn(1, 12)); // ARP lock.
      await held('TRIG 3', () => turn(3, 20)); // GATE lock.
      const armed = await masks();
      expect(armed.get(1)).toBe(1);
      expect(armed.get(3)).toBe(1 << 3);

      await held('TRIG 1', () => tap('RECORD'));
      await waitCount(0, 1, 'stock trig copy retained the attachment');
      await held('TRIG 3', () => tap('STOP'));
      await waitCount(1, 1, 'stock trig paste installed the source attachment');
      expect((await masks()).get(3)).toBe(1);
      await held('TRIG 3', () => tap('STOP'));
      await waitCount(3, 1, 'undo paste restored the destination attachment');
      expect((await masks()).get(3)).toBe(1 << 3);
      expect(await nativeTrig(3), 'undo paste retains the original native trig').toBe(1);

      await held('TRIG 1', () => tap('PLAY'));
      await waitCount(2, 1, 'stock trig clear removed the source attachment');
      expect((await masks()).has(1)).toBe(false);
      await held('TRIG 1', () => tap('PLAY'));
      await waitCount(3, 2, 'undo clear restored the source attachment');
      expect((await masks()).get(1)).toBe(1);

      await held('TRIG 1', () => held('TRIG 3', () => tap('RECORD')));
      await waitCount(0, 2, 'the clipboard retained both selected attachments');
      await held('TRIG 16', () => tap('STOP'));
      await waitCount(1, 2, 'the first paste wraps a 16-step track');
      expect(Object.fromEntries(await masks())).toMatchObject({ 16: 1, 2: 1 << 3 });
      await held('TRIG 16', () => tap('STOP'));
      await waitCount(3, 3, 'undo restores both wrapped destinations');
      expect((await masks()).has(16)).toBe(false);
      expect((await masks()).has(2)).toBe(false);

      await held('TRIG 6', () => tap('STOP'));
      await waitCount(1, 3, 'the same clipboard pastes again after undo');
      expect((await masks()).get(6)).toBe(1);
      expect((await masks()).get(8)).toBe(1 << 3);
      await held('TRIG 6', () => tap('STOP'));
      await waitCount(3, 4, 'undo restores the second paste destinations');

      await tap('TRIG 1');
      expect((await masks()).has(1), 'deleting the native trig removes its attachment').toBe(false);
      expect((await masks()).get(3), 'trig deletion preserves neighbors').toBe(1 << 3);
      expect(await nativeTrig(1)).toBe(0);
      expect(await nativeTrig(3)).toBe(1);
      await session.expectNoFault('individual trig clipboard');
    },
  );
});
