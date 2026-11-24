import { readFileSync } from 'node:fs';
import path from 'node:path';
import { Session } from '@digi/test-support';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { liveOptions } from '@digi/regression/runtime/live-options.ts';
import { REDRAW, SETTLE } from '@digi/regression/runtime/fx-live.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { cardAfter, overlayRecords } from '@digi/regression/runtime/write-safety.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';
import { trackKeyboardCard, trackKeyboardSymbols } from './track-card.ts';

describeOn('dt2', 'shared track keyboard project storage', () => {
  const options = trackKeyboardCard(liveOptions('dt2', { fresh: true }));
  const handle = withSession({ ...options, dsp: 'sharc', restart: false });
  test(
    'preserves independent pattern settings through paste, undo, and a saved-project cold boot',
    { timeout: 240_000 },
    async () => {
      const { session } = handle;
      const image = await prepareRuntimePlanModulesOffline(
        session.options,
        session.runDir,
        session.runDir,
        ['DKRT_BOOT', 'tk_storage_info', 'pattern_slots'],
        { modules: ['core/track-keyboard', 'core/health'] },
      );
      const boot = async (target: Session, card: string | undefined) => {
        const epoch = Number((await target.status()).epoch);
        await target.client.firmwareLoad({
          syx: image.syx,
          base_sha256: image.plan.base.syx_sha256,
          start: 'auto',
          force: true,
          auto_no: true,
          card,
        });
        await target.client.call('run.mode', { mode: 'free' });
        await expectRuntimeUi(target, image.plan, target.runDir, { afterEpoch: epoch });
        return trackKeyboardSymbols(target, image.plan);
      };
      const panel = (target: Session, sym: (name: string) => number) => {
        const { tap, turn, edge } = acknowledgedPanel(target);
        const wait = (ms: number, why: string = SETTLE) => sessionRt(target).ui.wait({ ms, why });
        const modified = async (modifier: string, key: string) => {
          await edge(modifier, true);
          try {
            await tap(key);
          } finally {
            await edge(modifier, false);
          }
        };
        const storage = (word: number) =>
          target.client.memPeek(sym('tk_storage_info') + word * 4);
        const arp = async (step: number) => {
          const record = await target.client.memPeek(sym('pattern_slots') + (step - 1) * 4);
          if (!record) return 0;
          const word = await target.client.memPeek(record);
          if (word === null) throw new Error('Unreadable keyboard pattern record');
          return word >>> 24; // TkTrackConfig.params.arp at the start of track 1.
        };
        const claims = () => target.client.memPeek(sym('tk_panel_claim_hits'));
        const pattern = () => target.client.memPeek(0x4316501c);
        const select = async (step: number, expected: (p: number | null) => boolean) => {
          await modified('PTN', `TRIG ${step}`);
          await pollEmulated(target, pattern, expected, {
            emulatedMs: 5000,
            message: `selected pattern A${step}`,
          });
          await wait(1000);
        };
        const checkClaim = async (expected: boolean) => {
          const previous = await claims();
          expect(previous).not.toBeNull();
          await tap('KEYBOARD');
          await tap('TRIG 9');
          await tap('KEYBOARD');
          expect(await claims(), 'keyboard dispatch uses the selected pattern')
            .toBe((previous ?? 0) + Number(expected));
        };
        return { tap, turn, wait, modified, storage, arp, pattern, select, checkClaim };
      };

      const ui = panel(session, await boot(session, session.cardPath));
      await ui.modified('TRK', 'TRIG 1');
      const a = await ui.pattern();
      expect(a).toBeGreaterThan(0);
      await ui.tap(1);
      await ui.tap(1);
      await ui.tap(1);
      await ui.turn(1, 12); // ARP A, enable.
      expect(await ui.arp(1)).toBe(1);
      await ui.checkClaim(true);

      await ui.modified('FUNC', 'RECORD'); // Copy A01.
      await ui.wait(1000);
      await ui.select(2, (p) => p !== null && p !== a);
      const b = await ui.pattern();
      expect(b).toBeGreaterThan(0);
      await ui.checkClaim(false);
      const beforePaste = (await ui.storage(2)) ?? 0;
      await ui.modified('FUNC', 'STOP'); // Paste A01 into A02.
      await pollEmulated(session, () => ui.storage(2), (n) => (n ?? 0) > beforePaste, {
        emulatedMs: 5000,
        message: 'pattern paste copies the keyboard data',
      });
      expect(await ui.arp(2), 'paste takes A01’s ARP state').toBe(1);
      await ui.modified('FUNC', 'STOP'); // Undo paste.
      await ui.wait(1000);
      expect(await ui.arp(2), 'undo restores A02’s ARP state').toBe(0);
      await ui.checkClaim(false);

      await ui.select(1, (p) => p === a);
      await ui.checkClaim(true);
      await ui.modified('FUNC', 'SETTINGS');
      await ui.wait(300, REDRAW);
      await ui.tap('YES'); // Save to project slot 001.
      await pollEmulated(session, () => ui.storage(1), (n) => (n ?? 0) > 0, {
        emulatedMs: 10_000,
        message: 'native project writer serialized the keyboard data',
      });
      expect(await ui.storage(3), 'pattern record fit in native project storage').toBe(0);
      await ui.wait(3500);
      await session.expectNoFault('keyboard pattern copy, undo, and project save');

      const overlay = path.join(session.runDir, 'keyboard-project.overlay.bin');
      await session.client.call('card.overlay', { path: overlay });
      expect(Array.from(overlayRecords(readFileSync(overlay)), (entry) => entry.sector)
        .some((sector) => sector >= 0x58000 && sector < 0x60000), 'stock project save reached the card')
        .toBe(true);
      if (!session.cardPath) throw new Error('No card snapshot for cold boot');
      const savedCard = cardAfter(session.cardPath, overlay, path.join(session.runDir, 'keyboard-project-saved'));
      await session.client.call('run.mode', { mode: 'paused' });
      const cold = await Session.start({ ...options, card: undefined, dsp: 'sharc', env: { DIGI_COLD_RELAX_FS: '1' } });
      try {
        const restored = panel(cold, await boot(cold, savedCard));
        await restored.tap('SETTINGS');
        await restored.wait(300, REDRAW);
        for (let i = 0; i < 3; i++) {
          await restored.tap('YES');
          await restored.wait(500, REDRAW);
        }
        await restored.tap('YES');
        await pollEmulated(cold, () => restored.storage(0), (n) => (n ?? 0) > 0, {
          emulatedMs: 15_000,
          message: 'native project reader loaded the saved keyboard data on a cold boot',
        });
        await restored.wait(6000);
        await restored.modified('TRK', 'TRIG 1');
        await restored.select(1, (p) => p === a);
        await restored.checkClaim(true);
        await restored.select(2, (p) => p === b);
        await restored.checkClaim(false);
        await cold.expectNoFault('keyboard project cold reload');
      } finally {
        await cold.close();
      }
    },
  );
});
