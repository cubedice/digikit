import { copyFileSync, mkdirSync, mkdtempSync } from 'node:fs';
import path from 'node:path';
import { repoRoot } from '@digi/test-support';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { trackType } from '@digi/regression/runtime/project-kit.ts';
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
    'preserves stock audio and MIDI TRIG pages and plays ARP while stopped',
    {
      timeout: 240_000,
    },
    async () => {
      const { session } = handle;
      const results = path.join(repoRoot(), 'dist/track-keyboard');
      mkdirSync(results, { recursive: true });
      const captures = mkdtempSync(path.join(results, 'panel-'));
      const capture = async (name: string) =>
        copyFileSync(await captureRemoteOled(session, name), path.join(captures, name + '.png'));
      const pixels = async () => {
        const frame = await session.client.framebufferGet({ data: true });
        const bytes = Buffer.from(frame.data_b64 ?? '', 'base64');
        expect(bytes.length).toBe(1024);
        return bytes;
      };
      const drawnText = async () => {
        const bytes = await pixels();
        let lit = 0;
        for (let x = 23; x < 122; x++) {
          for (let y = 12; y < 63; y++) {
            const cell = bytes[8 * x + 7 - Math.floor(y / 8)] ?? 0;
            lit += (cell >> (y % 8)) & 1;
          }
        }
        return lit;
      };
      const { edge, tap, turn } = acknowledgedPanel(session);
      const modified = async (modifier: string, key: string) => {
        await edge(modifier, true);
        try {
          await tap(key);
        } finally {
          await edge(modifier, false);
        }
      };
      const readPages = async (build: string) => {
        const views = new Map<string, Buffer>();
        for (const [kind, track] of [['audio', 1], ['midi', 2]] as const) {
          await modified('TRK', `TRIG ${track}`);
          if (kind === 'midi' && (await trackType(session, track)) !== 5) {
            await tap('SRC');
            await modified('FUNC', 'SRC');
            // The list remembers its cursor. Begin at the first machine.
            for (let index = 0; index < 12; index++) await tap('UP');
            for (let index = 0; index < 6; index++) await tap('DOWN');
            await tap('YES');
            await tap('YES');
            await pollEmulated(session, () => trackType(session, track), (type) => type === 5, {
              emulatedMs: 5000,
              message: 'track 2 uses the stock MIDI machine',
            });
          }
          await tap('SRC');
          for (let page = 1; page <= 2; page++) {
            await tap('PAGE TRIG');
            await session.wait(1500);
            const name = `${kind}-trig-${page}`;
            views.set(name, await pixels());
            await capture(`${build}-${name}`);
          }
        }
        return views;
      };
      const protectedPixels = (frame: Buffer) => {
        const result: number[] = [];
        for (let x = 0; x <= 122; x++) {
          // Right-side page marks and the temporary page title can change.
          // The track identifier and every pixel in the native body stay comparable.
          const firstRow = x < 17 ? 0 : 11;
          for (let y = firstRow; y < 64; y++) {
            const cell = frame[8 * x + 7 - Math.floor(y / 8)] ?? 0;
            result.push((cell >> (y % 8)) & 1);
          }
        }
        return Buffer.from(result);
      };
      await session.client.call('run.mode', { mode: 'free' });
      await pollEmulated(session, () => session.status(), (status) => status.cf.main_ui_reached, {
        emulatedMs: 30_000,
        message: 'unmodified firmware reached the stock UI',
      });
      await tap('NO'); // Dismiss the stock first-boot filesystem message.
      await pollEmulated(session, () => session.status(), (status) => status.cf.ready, {
        emulatedMs: 30_000,
        message: 'stock setup finished before capturing TRIG pages',
      });
      const stock = await readPages('stock');
      const image = await prepareRuntimePlanModulesOffline(
        session.options,
        session.runDir,
        session.runDir,
        ['DKRT_BOOT', 'tk_panel_claim_hits', 'tk_native_audio_active'],
        {
          modules: ['core/track-keyboard', 'core/instrument-play', 'core/digikit-menu', 'core/health'],
          timeoutMs: 90_000,
        },
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
      const shared = await readPages('shared');
      for (const [name, reference] of stock) {
        expect(protectedPixels(shared.get(name)!), `${name} preserves stock pixels`).toEqual(
          protectedPixels(reference),
        );
      }
      await modified('TRK', 'TRIG 1');
      await tap('SRC');
      await tap(1);
      await tap(1);
      const stockLeft = (await pixels()).subarray(0, 20 * 8);
      await capture('shared-audio-trig-2-before-arp');
      await tap(1);
      await turn(1, 12);
      await turn(3, 30); // Lengthen the generated gate to be observable by the emulator client.
      await capture('track-arp');
      expect((await pixels()).subarray(0, 20 * 8)).toEqual(stockLeft);
      expect(await drawnText(), 'ARP control labels render below the header').toBeGreaterThan(135);
      await tap('KEYBOARD');
      // The generated route skips the stock audition counter. Track bit 0
      // reports an accepted onset still awaiting its backend release.
      const held = async () =>
        ((await session.client.memPeek(planSymbol(image.plan, 'tk_native_audio_active'))) ?? 0) & 1;
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
