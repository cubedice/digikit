import path from 'node:path';
import { encoder } from '@digi/test-support';
import { expect, test } from 'vitest';
import { bounded, captureNote, dspWords, frames, pickSine, readFloatWav, rows, word } from '@digi/regression/runtime/fx-live.ts';
import { pageKit } from '@digi/regression/runtime/fx-pages-ui.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { selectTrack } from '@digi/regression/runtime/fx-track-ui.ts';
import { record } from '@digi/regression/runtime/artifacts.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { advance, mainPage, openPages, openPrismPage, tap } from './ui-helpers.ts';

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
        'core/phrase-route',
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

  test('keeps separate capture banks and healthy audio while both loops process CANOPY', async () => {
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
    const value = (index: number) => cf('phrase_ui_values', index);
    const turn = async (name: string, delta: number) => {
      await bounded(() => session.client.inputTurn(encoder(name), delta));
      await advance(session, 60);
    };
    const setPhrase = async (index: number, name: string, target: number, discrete = false) => {
      for (let attempt = 0; attempt < 32; attempt++) {
        const current = await value(index);
        if (current === target) return;
        await turn(name, discrete ? Math.sign(target - current) * 4 : Math.max(-8, Math.min(8, target - current)));
      }
      expect(await value(index), `${name} reaches ${target}`).toBe(target);
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
    // Read the actual native page while using the global FX gesture. DOWN
    // belongs to PRISM's local pages and cannot select another global effect.
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

    await openPrismPage(session, planSymbol(plan, 'phrase_ui_debug'));
    expect(await cf('phrase_ui_debug', 6), 'PRISM owns its native page').not.toBe(0);
    expect(await cf('phrase_ui_debug', 1), 'a fresh owner shows DELAY').toBe(4);
    // This test is about the held phrase beside Overloop: the tape head off (BAL at the grains), then LOOP.
    await setPhrase(45, 'ENC G', 127);
    await tap(session, 'DOWN', 80);
    expect(await cf('phrase_ui_debug', 1)).toBe(0);
    await setPhrase(2, 'ENC C', 1);
    const secondAudio = await audio('canopy-to-prism', () => setPhrase(0, 'ENC A', 1, true));
    const heldPhrase = await phrase();
    expect(heldPhrase[4]).toBe(0);
    expect(heldPhrase[5], 'PRISM captured selected CANOPY track 1').toBe(1);
    expect(heldPhrase[8]).toBeGreaterThan(32);
    expect(heldPhrase[20], 'PRISM take completes once').toBe(0);
    await setPhrase(20, 'LEVEL', 64);
    const beforeLoop = await loop(),
      beforePhrase = await phrase();
    expect(beforeLoop[6], 'Overloop contributes its held audio').toBeGreaterThan(0);
    expect(beforePhrase[23], 'PRISM contributes its held audio').toBeGreaterThan(0);
    const mixedAudio = await audio('all-three-active');
    const afterLoop = await loop(),
      afterPhrase = await phrase();
    expect(afterLoop[1]).toBeGreaterThan(beforeLoop[1]!);
    expect(afterPhrase[2]).toBeGreaterThan(beforePhrase[2]!);
    expect(afterLoop[3]).toBe(heldLoop[3]);
    expect(afterPhrase[8]).toBe(heldPhrase[8]);
    expect(afterLoop[10]).toBe(0);
    expect(afterPhrase[4]).toBe(0);
    expect(afterPhrase[30], 'selective route did not fail open').toBe(0);
    expect((await dsp('_ir_rows', 16))[0], 'CANOPY remains admitted while both FX run').toBe(3);
    expect((await dsp('_iv_result', 1))[0]! | 0, 'instrument process has no fault').toBeGreaterThanOrEqual(0);
    const chain = await rows(session, plan, context.slots);
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
      scope: 'One native three-module capture/blend scenario; no physical or worst-case budget qualification.',
    });
    await tap(session, 'STOP', 80);
    await bounded(() => session.expectNoFault('PRISM, CANOPY and Overloop active coexistence'));
  }, 360_000);
});
