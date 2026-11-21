import { copyFileSync, mkdirSync, mkdtempSync } from 'node:fs';
import path from 'node:path';
import { repoRoot } from '@digi/test-support';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';

describeOn('dt2', 'shared track keyboard panel proof', () => {
  const handle = withSession({ ...liveOptions('dt2', { fresh: true }), dsp: 'sharc', restart: false });
  test(
    'navigates to ARP and sustains native generated notes while transport is stopped',
    {
      timeout: 180_000,
    },
    async () => {
      const { session } = handle;
      const results = path.join(repoRoot(), 'dist/track-keyboard');
      mkdirSync(results, { recursive: true });
      const captures = mkdtempSync(path.join(results, 'panel-'));
      const capture = async (name: string) =>
        copyFileSync(await captureRemoteOled(session, name), path.join(captures, name + '.png'));
      const drawnText = async () => {
        const frame = await session.client.framebufferGet({ data: true });
        const bytes = Buffer.from(frame.data_b64 ?? '', 'base64');
        expect(bytes.length).toBe(1024);
        let lit = 0;
        for (let x = 23; x < 122; x++) {
          for (let y = 12; y < 63; y++) {
            const cell = bytes[8 * x + 7 - Math.floor(y / 8)] ?? 0;
            lit += (cell >> (y % 8)) & 1;
          }
        }
        return lit;
      };
      const image = await prepareRuntimePlanModulesOffline(
        session.options,
        session.runDir,
        session.runDir,
        ['DKRT_BOOT', 'tk_panel_claim_hits'],
        { modules: ['core/track-keyboard', 'core/instrument-play', 'core/digikit-menu', 'core/health'] },
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
      const { edge, tap, turn } = acknowledgedPanel(session);
      await edge('TRK', true);
      try {
        await tap('TRIG 1');
      } finally {
        await edge('TRK', false);
      }
      await tap(1);
      await tap(1);
      await tap(1);
      await turn(1, 12);
      await turn(3, 30); // Lengthen the generated gate to be observable by the emulator client.
      await capture('track-arp');
      expect(await drawnText(), 'ARP control labels render below the header').toBeGreaterThan(135);
      await tap('KEYBOARD');
      // Audition counter of native track 1, incremented by native note-on and
      // decremented by native note-off. The proof stops the physical keyboard
      // path from dispatching the original note and must drive this via ARP.
      const held = () => session.client.memPeek(0x44e086ec);
      const claims = () => session.client.memPeek(planSymbol(image.plan, 'tk_panel_claim_hits'));
      await edge('TRIG 9', true);
      try {
        await pollEmulated(session, claims, (n) => n === 1, {
          emulatedMs: 4000,
          message: 'shared keyboard owns the panel key',
        });
        await pollEmulated(session, held, (n) => n === 1, {
          emulatedMs: 4000,
          intervalMs: 10,
          message: 'native generated note-on after keyboard press',
        });
      } finally {
        await edge('TRIG 9', false);
      }
      await pollEmulated(session, held, (n) => n === 0, {
        emulatedMs: 4000,
        intervalMs: 10,
        message: 'paired native release after keyboard release',
      });
      expect(await held()).toBe(0);
      await tap(48); // Encoder H, CHRD on ARP.
      await tap('DOWN');
      await tap('YES'); // LEARN closes the list so the panel can play.
      await tap('TRIG 9');
      await tap('TRIG 11');
      await tap('TRIG 13');
      await tap(48);
      await capture('track-notes');
      await tap('YES'); // Apply USER and return to ARP.
      await tap('KEYBOARD');
      await tap(1);
      await capture('track-keys');
      expect(await drawnText(), 'KEYS control labels render below the header').toBeGreaterThan(135);
      await session.expectNoFault('shared track keyboard');
    },
  );
});
