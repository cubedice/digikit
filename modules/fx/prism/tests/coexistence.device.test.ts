import path from 'node:path';
import { encoder } from '@digi/test-support';
import { expect, test } from 'vitest';
import {
  bounded,
  captureNote,
  dspWords,
  frames,
  pickSine,
  readFloatWav,
  rows,
  word,
  REDRAW,
} from '@digi/regression/runtime/fx-live.ts';
import { pageKit } from '@digi/regression/runtime/fx-pages-ui.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { selectTrack, setAmount } from '@digi/regression/runtime/fx-track-ui.ts';
import { record } from '@digi/regression/runtime/artifacts.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { PH, advance, mainPage, openPages, openPrismPage, setPrism, tap } from './ui-helpers.ts';

// The set an installed device runs: the instrument, the two effects and the remote control (a ColdFire-only service).
const inventory = ['digikit/canopy', 'digikit/overloop', 'digikit/prism', 'digikit/remote'];

describeOn('dt2', 'DTII PRISM alongside CANOPY and Overloop', () => {
  const context = withPreparedFxSession('dt2', 'PRISM coexistence', {
    image: {
      name: 'prism-coexistence',
      modules: [
        'core/runtime',
        'core/dynreg',
        'core/dyndsp',
        'core/dynfx',
        'core/input',
        'core/instrument-play',
        'core/loop-storage',
        'core/loop-display',
        'core/phrase-ui',
        'core/digikit-menu',
        'core/health',
      ],
      inventory,
      symbols: [
        'DKRT_BOOT',
        'DKRT_INSTRUMENT_FILE',
        'fx.rows',
        'ip_control',
        '_ir_rows',
        '_iv_result',
        '_loop_buffer_info',
        '_loop_ui_status',
        '_phrase_storage_info',
        '_phrase_status',
        'phrase_ui_debug',
        'phrase_ui_values',
      ],
      policy: 'fx-routes',
    },
    initial: 'together',
    presets: { together: { selection: ['modules/m0.dkm', 'modules/m1.dkm', 'modules/m2.dkm', 'modules/m3.dkm'] } },
    requireAudible: true,
    // CANOPY's instrument load holds the UI task for 1.04 s once per boot (finding 350, "The main loop during the load"): the main loop
    // does not beat meanwhile. The limit stays well below a second stall of that size.
    stallMs: 1500,
  });

  test('keeps separate banks and healthy audio while Overloop loops CANOPY and PRISM repeats and holds its send', async () => {
    const { fixture, plan, profile, artifacts } = context;
    const runtime = fixture.runtime;
    const session = runtime.session;
    // CANOPY's DSP part (a 147 KiB file) is not done within the 30 s the shared boot inspection waits, and the two effects load after
    // it: wait for the load results first (1 while a DSP part is still going).
    const cf = (name: string, index = 0) => word(session, planSymbol(plan, name) + index * 4);
    const bootAt = planSymbol(plan, 'DKRT_BOOT');
    await bounded(() => session.client.call('run.mode', { mode: 'free' }));
    await pollEmulated(
      session,
      async () => ({
        results: await Promise.all(inventory.map(async (_, i) => (await word(session, bootAt + 40 + i * 4)) | 0)),
        // the V2 instrument transaction: phase, record, result, cause (DKRT_INSTRUMENT_FILE)
        instrument: await Promise.all([0, 1, 2, 3].map((i) => cf('DKRT_INSTRUMENT_FILE', i))),
      }),
      (state) => state.results.every((result) => result === 0),
      { emulatedMs: 60_000, intervalMs: 1000, message: 'the load results of the modules' },
    );
    const boot = await inspectFxBoot(runtime, plan, inventory, artifacts, 'boot');
    context.rememberEpoch(boot.epoch);
    const dsp = (name: string, count: number) => dspWords(session, planSymbol(plan, name), count);
    const loop = () => dsp('_loop_ui_status', 16);
    const phrase = () => dsp('_phrase_status', 32);
    const status = (words: number[], name: string) => words[PH[name]! / 4]!;
    const turn = async (name: string, delta: number) => {
      await bounded(() => session.client.inputTurn(encoder(name), delta));
      await advance(session, 60);
    };
    const audio = async (tag: string, during?: () => Promise<void>) => {
      const file = path.join(artifacts, `${tag}.wav`);
      await captureNote(session, {
        file,
        tag,
        trigger: (await frames(session)) + 300,
        length: 1200,
        during,
      });
      const samples = readFloatWav(file);
      let peak = 0,
        energy = 0,
        nonfinite = 0;
      for (const sample of samples) {
        if (!Number.isFinite(sample)) {
          nonfinite++;
          continue;
        }
        peak = Math.max(peak, Math.abs(sample));
        energy += sample * sample;
      }
      const result = { samples: samples.length, peak, rms: Math.sqrt(energy / Math.max(1, samples.length)), nonfinite };
      record(artifacts, `${tag}-audio`, result);
      expect(result.samples).toBeGreaterThan(1000);
      expect(result.nonfinite).toBe(0);
      expect(result.peak, `${tag} contains real native audio`).toBeGreaterThan(1e-5);
      return result;
    };

    // Read the actual installed leases; no assumed addresses or fabricated RAM.
    const legacy = await dsp('_loop_buffer_info', 4);
    const storage = await dsp('_phrase_storage_info', 6);
    expect(legacy.slice(0, 2)).toEqual([0x4c425546, 1]);
    expect(storage.slice(0, 2)).toEqual([0x50484231, 1]);
    const banks = [
      { name: 'Overloop', first: legacy[2]!, bytes: legacy[3]! },
      { name: 'phrase A', first: storage[2]!, bytes: storage[4]! },
      { name: 'phrase B', first: storage[3]!, bytes: storage[4]! },
    ];
    for (const bank of banks) {
      expect(bank.first).toBeGreaterThan(0);
      expect(bank.bytes).toBeGreaterThan(0);
      expect(bank.first + bank.bytes).toBeLessThanOrEqual(0x1_0000_0000);
    }
    for (let a = 0; a < banks.length; a++)
      for (let b = a + 1; b < banks.length; b++) {
        const left = banks[a]!,
          right = banks[b]!;
        expect(
          left.first + left.bytes <= right.first || right.first + right.bytes <= left.first,
          `${left.name} and ${right.name} have distinct reservations`,
        ).toBe(true);
      }
    expect(storage[5]).toBeGreaterThan(0);
    expect(storage[5]! * 8).toBeLessThan(storage[4]!);
    expect((await dsp('_ir_rows', 16))[0], 'CANOPY instance is admitted').toBe(3);
    record(artifacts, 'storage-and-registration', { banks, legacy, storage, boot });

    // Only CANOPY is a machine in this inventory. Stock chooser DOWN clamps at
    // its final entry, matching the existing CANOPY native acceptance setup.
    await selectTrack(sessionRt(session), 1);
    await pickSine(sessionRt(session), 20);
    expect(await cf('ip_control'), 'native chooser selected the admitted instrument').toBe(3);
    await mainPage(session);
    await openPages(session);
    // Read the actual native page while stepping through the FX view with its global gesture.
    for (let attempt = 0; attempt < 8 && (await cf('phrase_ui_debug', 7)) !== profile.fx.page_first; attempt++)
      await tap(session, 'FX', 100, 'FUNC');
    expect(await cf('phrase_ui_debug', 7), 'first appended FX is Overloop by boot order').toBe(profile.fx.page_first);
    const loopKit = pageKit(sessionRt(session), profile, 0);
    expect((await loop())[0]).toBe(0x4f564c31);
    // Overloop's knobs follow its parameter words on this contract: REC, START, FADE, LENGTH on A to D
    for (let attempt = 0; attempt < 16 && (await loop())[15] === 0; attempt++) await turn('ENC D', 1);
    const requested = (await loop())[15]!;
    expect(requested).toBeGreaterThan(0);
    expect(requested, 'bounded short take for coexistence').toBeLessThanOrEqual(4);
    await loopKit.setRaw('ENC A', 0, 0x7f00);
    expect((await loop())[2], 'Overloop armed through its native REC knob').toBe(3);
    const firstAudio = await audio('canopy-to-overloop', () => tap(session, 'PLAY', 80));
    const heldLoop = await loop();
    expect(heldLoop[2], 'Overloop captured and froze once').toBe(2);
    expect(heldLoop[3]).toBeGreaterThan(32);
    expect(heldLoop[10]).toBe(0);
    await loopKit.setRaw('ENC C', 2, 0x2200);

    // PRISM is a send effect: it hears CANOPY's track by the PRS amount on the second page of the track's FX key.
    const row = (await rows(session, plan, context.slots))[1]!;
    expect(row.slice(18, 23), 'PRISM runs on the send bus, with no amount yet').toEqual([1, 0, 0, 0, 0]);
    await mainPage(session);
    const rt = sessionRt(session);
    await rt.ui.tap('FX', { until: { ms: 400, why: REDRAW } });
    await rt.ui.tap('FX', { until: { ms: 400, why: REDRAW } });
    await setAmount(rt, profile, 'ENC A', 0, 100);
    await openPrismPage(session, planSymbol(plan, 'phrase_ui_debug'));
    expect(await cf('phrase_ui_debug', 6), 'PRISM owns its native page').not.toBe(0);
    // a short echo that repeats for a while, so that the tape holds the note when HOLD closes the input
    const values = planSymbol(plan, 'phrase_ui_values');
    await setPrism(session, values, PH.PH_P_ZONE!, 'ENC A', 3, { detent: true });
    await setPrism(session, values, PH.PH_P_RPTS!, 'ENC C', 80);
    const secondAudio = await audio('canopy-to-prism');
    const repeating = await phrase();
    expect(status(repeating, 'PH_S_MAGIC')).toBe(PH.PH_STATUS_MAGIC);
    expect(status(repeating, 'PH_S_ERROR')).toBe(PH.PH_ERROR_NONE);
    expect(status(repeating, 'PH_S_STATE'), 'PRISM repeats the live send').toBe(PH.PH_READY);
    await tap(session, 'PUSH C', 120);
    expect(await cf('phrase_ui_values', PH.PH_P_HOLD!), 'HOLD on the press of C').toBe(1);
    await advance(session, 500);
    const beforeLoop = await loop(),
      beforePhrase = await phrase();
    expect(beforeLoop[6], 'Overloop contributes its held audio').toBeGreaterThan(0);
    expect(status(beforePhrase, 'PH_S_STATE'), 'PRISM holds what is on its tape').toBe(PH.PH_HELD);
    expect(status(beforePhrase, 'PH_S_TAPE'), 'the tape has been written').toBeGreaterThan(32);
    const mixedAudio = await audio('all-three-active');
    const afterLoop = await loop(),
      afterPhrase = await phrase();
    expect(afterLoop[1]).toBeGreaterThan(beforeLoop[1]!);
    expect(status(afterPhrase, 'PH_S_HEARTBEAT'), 'PRISM ran every block meanwhile').toBeGreaterThan(
      status(beforePhrase, 'PH_S_HEARTBEAT'),
    );
    expect(afterLoop[3]).toBe(heldLoop[3]);
    expect(afterLoop[10]).toBe(0);
    expect(status(afterPhrase, 'PH_S_ERROR')).toBe(PH.PH_ERROR_NONE);
    expect(status(afterPhrase, 'PH_S_STATE')).toBe(PH.PH_HELD);
    expect((await dsp('_ir_rows', 16))[0], 'CANOPY remains admitted while both FX run').toBe(3);
    expect((await dsp('_iv_result', 1))[0]! | 0, 'instrument process has no fault').toBeGreaterThanOrEqual(0);
    const chain = await rows(session, plan, context.slots);
    expect(chain[0]![18], 'Overloop runs on the master').toBe(0);
    expect(chain[1]!.slice(18, 20), 'PRISM runs on the send bus with the amount of track 1').toEqual([1, 100]);
    for (const [index, name] of ['Overloop', 'PRISM'].entries()) {
      expect(chain[index]![12], `${name} process was called`).toBe(1);
      expect(chain[index]![14], `${name} is enabled`).toBe(0);
      expect(chain[index]![17], `${name} production FX guard has no fault`).toBe(0);
    }
    expect(await dsp('_loop_buffer_info', 4)).toEqual(legacy);
    expect(await dsp('_phrase_storage_info', 6)).toEqual(storage);
    record(artifacts, 'active-coexistence', {
      firstAudio,
      secondAudio,
      mixedAudio,
      beforeLoop,
      afterLoop,
      beforePhrase,
      afterPhrase,
      chain,
      scope: 'One native three-module scenario; no physical or worst-case budget qualification.',
    });
    await tap(session, 'STOP', 80);
    await bounded(() => session.expectNoFault('PRISM, CANOPY and Overloop active coexistence'));
  }, 360_000);
});
