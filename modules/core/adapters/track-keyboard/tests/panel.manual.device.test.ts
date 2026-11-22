import { copyFileSync, mkdirSync, mkdtempSync } from 'node:fs';
import path from 'node:path';
import { repoRoot } from '@digi/test-support';
import { portAnchorAddress } from '@digi/regression/runtime/device-data.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { word } from '@digi/regression/runtime/fx-live.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { selectDt2, trackType } from '@digi/regression/runtime/project-kit.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
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
      const crop = (frame: Buffer, x0: number, y0: number, x1: number, y1: number) => {
        const result: number[] = [];
        for (let x = x0; x <= x1; x++) {
          for (let y = y0; y <= y1; y++) {
            const cell = frame[8 * x + 7 - Math.floor(y / 8)] ?? 0;
            result.push((cell >> (y % 8)) & 1);
          }
        }
        return Buffer.from(result);
      };
      const drawnText = async () => crop(await pixels(), 23, 12, 122, 62).reduce((a, b) => a + b, 0);
      const trackColumn = (frame: Buffer) => crop(frame, 0, 11, 22, 63);
      const switchPixels = (frame: Buffer) => crop(frame, 23, 13, 46, 29);
      const ratePixels = (frame: Buffer) => crop(frame, 101, 13, 122, 29);
      const { edge, tap, turn } = acknowledgedPanel(session);
      const modified = async (modifier: string, key: string) => {
        await edge(modifier, true);
        try {
          await tap(key);
        } finally {
          await edge(modifier, false);
        }
      };
      const selectedTrackOperand = portAnchorAddress(
        session.options.device,
        session.options.firmware,
        'cf.track_keys.site.midi_selected_lit',
      );
      const selectedTrack = async () => word(session, await word(session, selectedTrackOperand));
      const readPages = async (build: string) => {
        const views = new Map<string, Buffer>();
        for (const [kind, track] of [['audio', 1], ['midi', 2]] as const) {
          await modified('TRK', `TRIG ${track}`);
          const selected = await selectedTrack();
          if (selected !== track - 1) await capture(`${build}-${kind}-track-select-failed`);
          expect(selected, `${build}: selected track`).toBe(track - 1);
          if (kind === 'midi' && (await trackType(session, track)) !== 5) {
            try {
              await selectDt2(sessionRt(session), track, 5, 6);
            } catch (error) {
              await capture(`${build}-midi-machine-select-failed`);
              throw error;
            }
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
      // The runtime hook can report ready while the stock FILE SYSTEM OK
      // acknowledgement still covers the page after the firmware reload.
      await tap('NO');
      const shared = await readPages('shared');
      for (const [name, reference] of stock) {
        expect(protectedPixels(shared.get(name)!), `${name} preserves stock pixels`).toEqual(
          protectedPixels(reference),
        );
      }
      await tap('PAGE TRIG');
      await capture('track-midi-arp-entry');
      await session.wait(1500);
      const midiArp = await pixels();
      expect(trackColumn(midiArp)).toEqual(trackColumn(shared.get('midi-trig-2')!));
      await capture('track-midi-arp');
      await tap('PAGE TRIG');
      await capture('track-midi-keys-entry');
      await session.wait(1500);
      expect(trackColumn(await pixels())).toEqual(trackColumn(shared.get('midi-trig-2')!));
      await capture('track-midi-keys');

      await modified('TRK', 'TRIG 1');
      await tap('SRC');
      await tap(1);
      await tap(1);
      const native = await pixels();
      const stockLeft = trackColumn(native);
      await capture('shared-audio-trig-2-before-arp');
      await tap(1);
      await capture('track-arp-entry');
      const rateCaption = (frame: Buffer) => crop(frame, 101, 32, 122, 36);
      const unfocusedCaption = rateCaption(await pixels());
      await turn(4, 12); // ARP defaults to 1/16. Stock RTRG defaults to 1/32.
      const focused = await pixels();
      expect(switchPixels(focused)).toEqual(switchPixels(native));
      expect(ratePixels(focused)).toEqual(ratePixels(native));
      expect(rateCaption(focused)).not.toEqual(unfocusedCaption);
      await capture('track-arp-rate-focus');
      await pollEmulated(session, pixels, (p) => rateCaption(p).equals(unfocusedCaption), {
        emulatedMs: 6000,
        message: 'the native edit-focus timer restores the RATE caption',
      });
      await capture('track-arp-rate-idle');
      await edge('PAGE TRIG', true);
      try {
        await pollEmulated(session, pixels, (p) => !rateCaption(p).equals(unfocusedCaption), {
          emulatedMs: 2000,
          message: 'holding TRIG PARAMETERS shows the ARP values',
        });
        expect(ratePixels(await pixels())).toEqual(ratePixels(native));
        await capture('track-arp-values');
      } finally {
        await edge('PAGE TRIG', false);
      }
      await pollEmulated(session, pixels, (p) => rateCaption(p).equals(unfocusedCaption), {
        emulatedMs: 2000,
        message: 'releasing TRIG PARAMETERS restores captions on ARP',
      });
      await turn(4, -12);
      await turn(1, 12);
      await turn(3, 30); // Lengthen the generated gate to be observable by the emulator client.
      await capture('track-arp');
      expect(trackColumn(await pixels())).toEqual(stockLeft);
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
