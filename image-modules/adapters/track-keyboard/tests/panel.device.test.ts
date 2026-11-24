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
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';
import { trackKeyboardCard, trackKeyboardSymbols } from './track-card.ts';

describeOn('dt2', 'shared track keyboard panel proof', () => {
  const handle = withSession({ ...trackKeyboardCard(liveOptions('dt2', { fresh: true })), dsp: 'sharc', restart: false });
  test(
    'preserves stock audio and MIDI TRIG pages and plays ARP while stopped',
    {
      timeout: 420_000,
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
      const hold = async (key: string, action: () => Promise<void>) => {
        await edge(key, true);
        try {
          await action();
        } finally {
          await edge(key, false);
        }
      };
      const modified = async (modifier: string, key: string) => {
        await hold(modifier, () => tap(key));
      };
      const cellCaption = (frame: Buffer, encoder: number) => {
        const x = 23 + 26 * (encoder % 4);
        const y = encoder < 4 ? 32 : 59;
        return crop(frame, x, y, Math.min(122, x + 24), y + 4);
      };
      const cellGraphic = (frame: Buffer, encoder: number) => {
        const x = 24 + 26 * (encoder % 4);
        const y = encoder < 4 ? 12 : 39;
        return crop(frame, x, y, Math.min(122, x + 22), y + 18);
      };
      const inverse = (pixels: Buffer) => Buffer.from(pixels.map((value) => 1 - value));
      const selectedTrackOperand = portAnchorAddress(
        session.options.device,
        session.options.firmware,
        'cf.track_keys.site.midi_selected_lit',
      );
      const selectedTrack = async () => word(session, await word(session, selectedTrackOperand));
      const readPages = async (build: string) => {
        const views = new Map<string, Buffer>();
        for (const [kind, track] of [
          ['audio', 1],
          ['midi', 2],
        ] as const) {
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
          await hold('PAGE TRIG', async () => {
            await session.wait(800);
            views.set(`${kind}-trig-2-values`, await pixels());
            await capture(`${build}-${kind}-trig-2-values`);
          });
          await session.wait(1500);
          views.set(`${kind}-trig-2-released`, await pixels());
          await capture(`${build}-${kind}-trig-2-released`);
          // Compare the native RATE or COND focus on an edited step before
          // the custom pages run. Remove the fixture trig after the capture.
          const encoder = kind === 'audio' ? 3 : 7;
          await tap('RECORD');
          await tap('TRIG 8');
          await hold('TRIG 8', async () => {
            const before = await pixels();
            for (let turnIndex = 0; turnIndex < 3; ++turnIndex) await turn(encoder + 1, 8);
            const focused = await pixels();
            await capture(`${build}-${kind}-trig-2-step-focus`);
            expect(cellGraphic(focused, encoder), `${build} edits native ${kind} step`).not.toEqual(
              cellGraphic(before, encoder),
            );
            views.set(`${kind}-trig-2-step-focus`, focused);
            await tap(`PUSH ${'ABCDEFGH'[encoder]}`);
          });
          await tap('TRIG 8');
          await tap('RECORD');
          await session.wait(2000);
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
      await pollEmulated(
        session,
        () => session.status(),
        (status) => status.cf.main_ui_reached,
        {
          emulatedMs: 30_000,
          message: 'unmodified firmware reached the stock UI',
        },
      );
      await tap('NO'); // Dismiss the stock first-boot filesystem message.
      await pollEmulated(
        session,
        () => session.status(),
        (status) => status.cf.ready,
        {
          emulatedMs: 30_000,
          message: 'stock setup finished before capturing TRIG pages',
        },
      );
      const stock = await readPages('stock');
      const image = await prepareRuntimePlanModulesOffline(
        session.options,
        session.runDir,
        session.runDir,
        ['DKRT_BOOT'],
        {
          modules: ['core/track-keyboard', 'core/health'],
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
      const sym = await trackKeyboardSymbols(session, image.plan);
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
      const midiKeys = await pixels();
      expect(trackColumn(midiKeys)).toEqual(trackColumn(shared.get('midi-trig-2')!));
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
        ((await session.client.memPeek(sym('tk_native_audio_active'))) ?? 0) & 1;
      const claims = () => session.client.memPeek(sym('tk_panel_claim_hits'));
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

      const checkPage = async (stem: string, isArp: boolean, nativeColumn: Buffer, valueColumn: Buffer) => {
        const controls = isArp ? [0, 1, 2, 3, 4, 5, 6, 7] : [0, 1, 2, 3, 6];
        const lockable = isArp ? controls : controls.slice(0, 4);
        // Approach the endpoints from both directions. The step writes then
        // keep the same displayed values, isolating inversion from the value change.
        const directions = isArp ? [1, 1, 1, 1, -1, 1, 1, 1] : [1, 1, -1, 1];
        const turns = isArp ? [2, 2, 2, 2, 2, 2, 2, 2] : [2, 2, 2, 6];
        for (const encoder of lockable) {
          for (let i = 0; i < turns[encoder]!; ++i) {
            const edit = () => turn(encoder + 1, 30 * directions[encoder]!);
            if ((isArp && encoder === 2) || (!isArp && encoder === 3)) {
              await hold(`PUSH ${'ABCDEFGH'[encoder]}`, edit);
            } else {
              await edit();
            }
          }
        }
        const labels = isArp ? midiArp : midiKeys;
        const changed = async (reference: Buffer) => {
          const frame = await pixels();
          return controls.filter((i) => !cellCaption(frame, i).equals(cellCaption(reference, i)));
        };
        await pollEmulated(
          session,
          () => changed(labels),
          (indices) => indices.length === 0,
          {
            emulatedMs: 9000,
            message: `${stem} focuses expire before collecting the idle frame`,
          },
        );
        const idle = await pixels();
        expect(trackColumn(idle), `${stem} idle track column`).toEqual(nativeColumn);
        await capture(`${stem}-idle`);
        await hold('PAGE TRIG', async () => {
          try {
            await pollEmulated(
              session,
              () => changed(idle),
              (indices) => indices.length === controls.length,
              { emulatedMs: 3000, message: `${stem} shows every parameter value` },
            );
          } finally {
            await capture(`${stem}-values`);
          }
          const shown = await pixels();
          expect(trackColumn(shown), `${stem} held track column`).toEqual(valueColumn);
          for (const i of controls) {
            expect(cellCaption(shown, i).includes(1), `${stem} visible value ${i + 1}`).toBe(true);
            expect(cellGraphic(shown, i), `${stem} preserves graphic ${i + 1} during hold`).toEqual(
              cellGraphic(idle, i),
            );
          }
        });
        await pollEmulated(
          session,
          () => changed(idle),
          (indices) => indices.length === 0,
          { emulatedMs: 2000, message: `${stem} restores every caption on release` },
        );
        await capture(`${stem}-released`);
        if (isArp) {
          // AUG is the last choice. The previous choice has four glyphs
          // and exercises the rightmost cell with a longer chord reading.
          await turn(8, -4);
          await hold('PAGE TRIG', async () => {
            await pollEmulated(
              session,
              () => changed(idle),
              (indices) => indices.length === controls.length,
              { emulatedMs: 3000, message: `${stem} holds the value view long enough to avoid a page turn` },
            );
            const reading = await pixels();
            const width = Array.from({ length: 22 }, (_, i) => 101 + i).filter((x) =>
              crop(reading, x, 59, x, 63).includes(1),
            ).length;
            expect(width, `${stem} renders a four-glyph chord at the right edge`).toBeGreaterThan(13);
            expect(crop(reading, 123, 59, 125, 63).includes(1), `${stem} preserves the right margin`).toBe(false);
            await capture(`${stem}-right-edge`);
          });
          await turn(8, 12);
        }

        await hold('TRIG 1', async () => {
          const before = await pixels();
          for (const i of lockable) {
            const edit = () => turn(i + 1, 12 * directions[i]!);
            if ((isArp && i === 2) || (!isArp && i === 3)) {
              await hold(`PUSH ${'ABCDEFGH'[i]}`, edit);
            } else {
              await edit();
            }
          }
          const missingInversion = async () => {
            const frame = await pixels();
            return lockable.filter((i) => !cellGraphic(frame, i).equals(inverse(cellGraphic(before, i))));
          };
          try {
            await pollEmulated(session, missingInversion, (indices) => indices.length === 0, {
              emulatedMs: 3000,
              message: `${stem} inverts every lockable widget at matching values`,
            });
          } finally {
            await capture(`${stem}-step-all-locked`);
          }
          // Keep the all-locked frame and verify each lock's independent clear path.
          for (const i of lockable) await tap(`PUSH ${'ABCDEFGH'[i]}`);
          await pollEmulated(
            session,
            pixels,
            (frame) => lockable.every((i) => cellGraphic(frame, i).equals(cellGraphic(before, i))),
            { emulatedMs: 3000, message: `${stem} restores each widget after step lock clear` },
          );
          await capture(`${stem}-step-cleared`);
        });
      };

      for (const [kind, track] of [
        ['audio', 1],
        ['midi', 2],
      ] as const) {
        await modified('TRK', `TRIG ${track}`);
        await tap('SRC');
        for (let page = 0; page < 3; ++page) await tap('PAGE TRIG');
        await tap('RECORD');
        await tap('TRIG 1');
        await tap('TRIG 3');
        const nativeColumn = trackColumn(shared.get(`${kind}-trig-2`)!);
        const valueColumn = trackColumn(shared.get(`${kind}-trig-2-values`)!);
        await checkPage(`${kind}-arp-matrix`, true, nativeColumn, valueColumn);
        const before = await pixels();
        await hold('TRIG 1', () => hold('TRIG 3', () => turn(4, 12)));
        for (const step of [1, 3]) {
          await hold(`TRIG ${step}`, async () => {
            try {
              await pollEmulated(
                session,
                async () => cellGraphic(await pixels(), 3).equals(inverse(cellGraphic(before, 3))),
                Boolean,
                { emulatedMs: 3000, message: `${kind} multi-step RATE lock reached step ${step}` },
              );
            } finally {
              await capture(`${kind}-arp-matrix-multi-${step}`);
            }
          });
        }
        await hold('TRIG 1', () => hold('TRIG 3', () => tap('PUSH D')));
        await tap('PAGE TRIG');
        await checkPage(`${kind}-keys-matrix`, false, nativeColumn, valueColumn);
        await tap('RECORD');
        const keysCaption = cellCaption(midiKeys, 0);
        const showingIdleKeys = async () => cellCaption(await pixels(), 0).equals(keysCaption);
        await pollEmulated(session, showingIdleKeys, Boolean, {
          emulatedMs: 9000,
          message: `${kind} KEYS encoder focus expires before opening Quantize`,
        });
        await capture(`${kind}-keys-matrix-before-quantize`);
        // A covering native Quantize view must return to KEYS without cycling TRIG.
        await modified('FUNC', 'PAGE TRIG');
        expect(await showingIdleKeys()).toBe(false);
        await capture(`${kind}-keys-matrix-quantize`);
        await tap('NO');
        try {
          await pollEmulated(session, showingIdleKeys, Boolean, {
            emulatedMs: 9000,
            message: `${kind} returns to KEYS from Quantize`,
          });
        } finally {
          await capture(`${kind}-keys-matrix-return`);
        }
      }
      await session.expectNoFault('shared track keyboard');
    },
  );
});
