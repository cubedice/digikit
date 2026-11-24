import fs from 'node:fs';
import path from 'node:path';
import { afterAll, beforeAll, expect, test, vi } from 'vitest';
import { repoRoot } from '@digi/test-support';
import { withSession } from '@digi/regression/support.ts';
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { waitInstrumentReady } from '@digi/regression/runtime/instrument-ready.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';
import { moduleArtifact } from '@digi/regression/runtime/install.ts';
import { seedModules, seedServices } from '@digi/regression/runtime/card-seed.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { canopyPanel } from '../../../../modules/machine/digikit/canopy/tests/canopy-panel.ts';
import { trackKeyboardSymbols } from './track-card.ts';

describeOn('dt2', 'CANOPY shared track keyboard', () => {
  vi.setConfig({ hookTimeout: 600_000 });
  const options = liveOptions('dt2', { fresh: true });
  const fixtureRoot = fs.realpathSync(path.join(repoRoot(), 'out'));
  const card = fs.mkdtempSync(path.join(fixtureRoot, 'track-keyboard-canopy-'));
  beforeAll(async () => {
    for (const name of ['1-sine220.wav', '2-sine880.wav', '3-noise.wav'])
      fs.copyFileSync(path.join(options.card!, name), path.join(card, name));
    await seedModules(card, 'dt2', [{ file: moduleArtifact('dt2', 'digikit/canopy'), needs: ['machines', 'instrument'] }]);
    await seedServices(card, 'dt2', ['track-keyboard']);
  });
  afterAll(() => {
    const owned = fs.realpathSync(card);
    if (path.dirname(owned) !== fixtureRoot || !path.basename(owned).startsWith('track-keyboard-canopy-'))
      throw new Error('Refusing to remove an unowned card fixture');
    fs.rmSync(owned, { recursive: true });
  });
  const handle = withSession({ ...options, card, dsp: 'sharc', restart: false });

  test('inherits legacy performance until a TRIG edit and then owns keyboard input', { timeout: 420_000 }, async () => {
    const { session } = handle;
    const output = fs.mkdtempSync(path.join(fixtureRoot, 'tk-canopy-ui-'));
    const { syx, plan } = await prepareRuntimePlanModulesOffline(
      session.options,
      session.runDir,
      output,
      [
        'DKRT_BOOT',
        '_iv_lock_slots',
        '_iv_shared_controls',
      ],
      { modules: ['core/track-keyboard', 'core/instrument-play', 'core/health'], timeoutMs: 90_000 },
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
    const dk = liveProtocol(session);
    await dk.select(['modules/canopy.dkm']);
    const epoch = Number((await session.status()).epoch);
    await dk.restart();
    await dk.waitForBoot();
    const sym = await trackKeyboardSymbols(session, plan, ['instrument']);
    const cf = async (name: string, index = 0) => {
      const value = await session.client.memPeek(sym(name) + 4 * index);
      if (value === null) throw new Error(`Missing ${name}[${index}]`);
      return value >>> 0;
    };
    const byteAt = async (addr: number) => {
      const value = await session.client.memPeek((addr & ~3) >>> 0);
      if (value === null) throw new Error(`Missing byte at 0x${addr.toString(16)}`);
      return (value >>> (24 - 8 * (addr & 3))) & 255;
    };
    const halfAt = async (addr: number) => ((await byteAt(addr)) << 8) | (await byteAt(addr + 1));
    const byte = (name: string, offset = 0) => byteAt(sym(name) + offset);
    await waitInstrumentReady(cf);
    await expectRuntimeUi(session, plan, output, { afterEpoch: epoch, emulatedMs: 180_000 });
    const { edge, tap, turn } = acknowledgedPanel(session);
    await edge('FUNC', true);
    try {
      await tap('SRC');
    } finally {
      await edge('FUNC', false);
    }
    for (let i = 0; i < 20; ++i) await tap('DOWN');
    await tap('YES');
    await tap('YES');
    await pollEmulated(
      session,
      () => byte('track_keyboard_src'),
      (n) => n === 1,
      {
        emulatedMs: 15000,
        message: 'CANOPY compact SRC view is registered',
      },
    );
    expect(await byte('track_keyboard_lanes')).toBe(0);
    const srcPage = () => cf('ip_src_debug', 1);
    const nextSrcPage = async (page: number) => {
      await tap('SRC');
      await pollEmulated(session, srcPage, (current) => current === page, {
        emulatedMs: 4000,
        message: `the native SRC cycle reaches page ${page + 1}`,
      });
    };
    const captureSrc = async (name: string) =>
      fs.copyFileSync(await captureRemoteOled(session, `tk-canopy-${name}`), path.join(output, `${name}.png`));
    await tap('SRC');
    for (let i = 0; i < 2 && (await srcPage()) !== 0; i++) await tap('SRC');
    expect(await srcPage()).toBe(0);
    await captureSrc('velvet-voice');
    await nextSrcPage(1);
    await captureSrc('velvet-fm-env');
    await nextSrcPage(2);
    await captureSrc('velvet-detail');
    await nextSrcPage(0);
    expect(await byte('track_keyboard_lanes')).toBe(0);
    await tap(1);
    await tap(1);
    await tap(1);
    await turn(1, 12);
    await pollEmulated(
      session,
      () => byte('track_keyboard_lanes'),
      (n) => n === 3,
      {
        emulatedMs: 5000,
        message: 'first performance edit hands both lanes to shared keyboard',
      },
    );
    const before = await cf('tk_panel_claim_hits');
    await tap('KEYBOARD');
    await tap('TRIG 9');
    await pollEmulated(
      session,
      () => cf('tk_panel_claim_hits'),
      (n) => n === before + 1,
      {
        emulatedMs: 4000,
        message: 'the shared owner takes the CANOPY panel note',
      },
    );
    await tap('KEYBOARD');
    const dsp = async (name: string, offset: number, len: number) => {
      const result = await session.client.call<{ hex: string }>('dsp.peek', {
        addr: sym(name) + offset,
        len,
      });
      const bytes = Buffer.from(result.hex, 'hex');
      return bytes.length === len ? bytes : null;
    };
    const mix = async () => (await dsp('_iv_shared_controls', 24, 4))?.readUInt32LE(0);
    const originalMix = await mix();
    expect(originalMix).toBeLessThan(127);
    await tap(1); // TRIG 4, KEYS
    await turn(1, -12);
    await pollEmulated(session, mix, (n) => n === 127, {
      emulatedMs: 5000,
      message: 'switching from both lanes to arp-only updates the DSP mix without a Sound edit',
    });
    expect(await byte('track_keyboard_lanes')).toBe(2);
    await turn(1, 12);
    await pollEmulated(session, mix, (n) => n === originalMix, {
      emulatedMs: 5000,
      message: 'restoring both lanes restores the saved CANOPY blend',
    });
    await tap('RECORD');
    const pattern = (await session.client.memPeek(0x4316501c)) ?? 0;
    expect(pattern, 'scheduled pattern is in ColdFire RAM').toBeGreaterThanOrEqual(0x40000000);
    expect(pattern).toBeLessThan(0x48000000);
    const firstStep = 0;
    const lockedStep = 4;
    const unlockedStep = 8;
    const hasNote = (flags: number) => (flags & 0x81) === 0x81;
    const trigFlags = (step: number) => halfAt(pattern + 2 * step);
    for (const step of [firstStep, lockedStep, unlockedStep]) {
      if (!hasNote(await trigFlags(step))) await tap(`TRIG ${step + 1}`);
      await pollEmulated(session, () => trigFlags(step), hasNote, {
        emulatedMs: 1000,
        message: `step ${step + 1} is a stock note trig`,
      });
    }
    await tap('SRC');
    expect(await cf('ip_src_debug', 1)).toBe(0);
    await edge(`TRIG ${lockedStep + 1}`, true);
    try {
      await turn(2, 20); // VOICE B is COLOR on Velvet.
    } finally {
      await edge(`TRIG ${lockedStep + 1}`, false);
    }
    await tap('RECORD');
    expect(hasNote(await trigFlags(lockedStep))).toBe(true);
    expect(hasNote(await trigFlags(unlockedStep))).toBe(true);
    const locks = pattern + 0x17e50;
    const lockMap = locks + 0x50a0;
    const colorLane = await byteAt(lockMap + 26);
    expect(colorLane, 'the panel saved the packed COLOR lock').toBeLessThan(80);
    const colorValues = locks + colorLane * 0x102 + 2;
    expect(await halfAt(colorValues + 2 * lockedStep), 'step 5 stores COLOR 84').toBe(((17 + 84) << 2) | 1);
    expect(await halfAt(colorValues + 2 * unlockedStep), 'step 9 has no COLOR lock').toBe(0xffff);

    // The panel records COLOR above. Seed the companion stock FREQ lock
    // directly in the emulator pattern, using native lane layout (finding 347).
    const cutoffIndex = 36;
    const lockedCutoff = 0x4300;
    expect(await byteAt(lockMap + cutoffIndex)).toBe(0xff);
    let cutoffLane = 0;
    while (cutoffLane < 80 && (await byteAt(locks + cutoffLane * 0x102)) !== 0xff) ++cutoffLane;
    expect(cutoffLane).toBeLessThan(80);
    const write = (addr: number, bytes: number[]) =>
      session.client.call('mem.cf.write', { addr, hex: Buffer.from(bytes).toString('hex') });
    const cutoffValues = locks + cutoffLane * 0x102 + 2;
    await write(cutoffValues - 2, [cutoffIndex, 0]);
    await write(cutoffValues + 2 * lockedStep, [lockedCutoff >>> 8, lockedCutoff & 255]);
    await write(lockMap + cutoffIndex, [cutoffLane]);
    const stepLockCount = locks + 0x5510 + lockedStep;
    expect(await byteAt(stepLockCount)).toBe(1);
    await write(stepLockCount, [2]);
    expect(await halfAt(cutoffValues + 2 * lockedStep)).toBe(lockedCutoff);
    expect(await halfAt(cutoffValues + 2 * unlockedStep)).toBe(0xffff);

    const slotCount = async () => (await dsp('_iv_lock_slots', 0, 4))?.readUInt32LE(0);
    const slotPair = async () => (await dsp('_iv_lock_slots', 4, 4))?.readUInt32LE(0);
    const color = async () => (await dsp('_iv_shared_controls', 72, 4))?.readUInt16LE(0);
    const baseColor = await color();
    expect(baseColor).toBe(64);
    expect(await byte('tk_canopy_fallback', 1)).toBe(0);
    const cutoff = () => halfAt(0x80003362 + 2 * cutoffIndex);
    const baseCutoff = await cutoff();
    expect(baseCutoff).toBeGreaterThan(lockedCutoff);
    const beforePlay = await cf('tk_sequence_audio_hits');
    await tap('PLAY');
    await pollEmulated(
      session,
      async () => ({
        sequenceHits: await cf('tk_sequence_audio_hits'),
        count: await slotCount(),
        color: await color(),
        cutoff: await cutoff(),
      }),
      (status) =>
        status.sequenceHits === beforePlay + 1 &&
        status.count === 0 &&
        status.color === baseColor &&
        status.cutoff === baseCutoff,
      {
        emulatedMs: 1000,
        intervalMs: 10,
        message: 'the cached first trig at START reaches CANOPY before the first locked trig',
      },
    );
    const locked = await pollEmulated(
      session,
      async () => {
        return {
          count: await slotCount(),
          pair: await slotPair(),
          color: await color(),
          cutoff: await cutoff(),
          hits: await cf('tk_sequence_audio_locked_hits'),
          sequenceHits: await cf('tk_sequence_audio_hits'),
        };
      },
      (status) =>
        status.count === 1 &&
        status.pair === ((2 << 10) | 84) &&
        status.color === 84 &&
        status.cutoff === lockedCutoff &&
        status.hits > 0,
      {
        emulatedMs: 5000,
        intervalMs: 10,
        message: 'the CANOPY phrase receives both its packed COLOR lock and its native filter lock',
      },
    );
    await pollEmulated(
      session,
      async () => ({
        count: await slotCount(),
        color: await color(),
        cutoff: await cutoff(),
        sequenceHits: await cf('tk_sequence_audio_hits'),
      }),
      (status) =>
        status.sequenceHits > locked.sequenceHits &&
        status.count === 0 && status.color === baseColor && status.cutoff === baseCutoff,
      {
        emulatedMs: 8000,
        intervalMs: 10,
        message: 'the next unlocked CANOPY phrase restores base COLOR and filter cutoff',
      },
    );
    await tap('STOP');

    // Save a contrasting CANOPY Sound through the stock pool menu, then
    // restore the active track before assigning the pool Sound to one trig.
    const family = async () => (await dsp('_iv_shared_controls', 4, 4))?.readUInt32LE(0);
    const tune = async () => (await dsp('_iv_shared_controls', 0, 4))?.readUInt16LE(2);
    const nativeTune = async () => {
      const sound = await cf('ip_admission', 1);
      expect(sound, 'CANOPY is editing an admitted native Sound').toBeGreaterThanOrEqual(0x40000000);
      return halfAt(sound + 0x46);
    };
    const panel = canopyPanel(session, plan, sym);
    const baseTune = await nativeTune();
    expect(baseTune).toBe(0x4000);
    // Keep the original Sound in slot 002 so it can be recalled exactly,
    // regardless of how many native TUNE turns the stock accumulator accepts.
    await panel.poolSave(1, 2);
    await panel.keysPage(1);
    await turn(1, 8); // Velvet to the first wavetable, two WAVE increments.
    await pollEmulated(session, family, (value) => value === 2, {
      emulatedMs: 5000,
      message: 'VOICE WAVE selects the wavetable family',
    });
    expect(await srcPage()).toBe(0);
    await captureSrc('wavetable-voice');
    await nextSrcPage(1);
    await captureSrc('wavetable-fm-env');
    await nextSrcPage(2);
    await captureSrc('wavetable-detail');
    await nextSrcPage(0);
    await turn(1, 20); // First wavetable to FM, five WAVE increments.
    await pollEmulated(session, family, (value) => value === 3, {
      emulatedMs: 5000,
      message: 'the contrasting Sound uses FM',
    });
    expect(await srcPage(), 'changing WAVE keeps VOICE selected').toBe(0);
    await captureSrc('fm-voice');
    await nextSrcPage(1);
    await captureSrc('fm-env');
    const attackPixels = async () => {
      const frame = await session.client.framebufferGet();
      const oled = Buffer.from(frame.data_b64 ?? '', 'base64');
      expect(oled.length).toBe(1024);
      const pixels: number[] = [];
      // Left side of the stock envelope, above the control names.
      for (let y = 14; y < 30; y++)
        for (let x = 24; x < 60; x++) pixels.push((oled[8 * x + 7 - (y >> 3)] >> (y & 7)) & 1);
      return Buffer.from(pixels);
    };
    const beforeAttack = await attackPixels();
    expect(beforeAttack.includes(1), 'the idle stock envelope is visible').toBe(true);
    await turn(1, 30);
    await pollEmulated(session, attackPixels, (pixels) => !pixels.equals(beforeAttack), {
      emulatedMs: 4000,
      message: 'the envelope graph pixels change when FM ENV A increases attack',
    });
    await captureSrc('fm-env-attack-longer');
    await turn(1, -30);
    await nextSrcPage(2);
    await captureSrc('fm-detail');
    const priorTurns = await cf('ip_src_debug', 4);
    // The stock encoder accumulator can consume the first detent batch.
    // Check the edited Sound directly and continue until the value changes.
    const tuneEdit = await pollEmulated(
      session,
      async () => {
        await turn(1, 8);
        await session.wait(500);
        return {
          raw: await nativeTune(),
          turns: (await cf('ip_src_debug', 4)) - priorTurns,
          encoder: await cf('ip_src_debug', 10),
          handlerResult: (await cf('ip_src_debug', 11)) | 0,
        };
      },
      (state) => state.raw !== baseTune,
      {
        emulatedMs: 5000,
        message: 'DETAIL A changes the native Sound TUNE before saving the pool entry',
      },
    ).catch(async (error) => {
      await panel.shot('source-native-tune-unchanged');
      throw error;
    });
    const poolTune = tuneEdit.raw;
    const activeSound = await cf('ip_admission', 1);
    await panel.poolSave(1);
    const poolSound = 0x4291377a;
    expect(await byteAt(poolSound + 0xa2)).toBe(await byteAt(activeSound + 0xa2));
    expect(await halfAt(poolSound + 0x46)).toBe(poolTune);
    await panel.poolLoad(1, 2);
    await pollEmulated(
      session,
      async () => ({ family: await family(), tune: await nativeTune() }),
      (state) => state.family === 0 && state.tune === baseTune,
      { emulatedMs: 5000, message: 'the base Sound is Velvet again with its original TUNE' },
    );
    const soundLock = pattern + 0x400 + lockedStep;
    expect(await byteAt(soundLock)).toBe(0xff);
    await write(soundLock, [0]); // The project pool is zero based.
    await tap('PLAY');
    const lockedSound = await pollEmulated(
      session,
      async () => ({
        family: await family(),
        tune: await tune(),
        cutoff: await cutoff(),
        hits: await cf('tk_sequence_audio_hits'),
      }),
      (state) => state.family === 3 && state.tune === poolTune && state.cutoff === lockedCutoff,
      {
        emulatedMs: 8000,
        intervalMs: 10,
        message: 'the generated CANOPY phrase uses the native Sound lock and stock cutoff lock',
      },
    );
    expect(await byte('tk_canopy_fallback', 1)).toBe(0);
    await pollEmulated(
      session,
      async () => ({ family: await family(), tune: await tune(), hits: await cf('tk_sequence_audio_hits') }),
      (state) => state.hits > lockedSound.hits && state.family === 0 && state.tune === baseTune,
      {
        emulatedMs: 8000,
        intervalMs: 10,
        message: 'the next phrase restores the track source and TUNE',
      },
    );
    await tap('STOP');
    await write(soundLock, [127]); // Empty pool entry on this disposable project.
    await pollEmulated(session, () => byte('tk_canopy_fallback', 1), (mask) => (mask & 1) !== 0, {
      emulatedMs: 5000,
      message: 'an unsupported Sound record leaves native performance routing active',
    });
    await write(soundLock, [0xff]);
    await session.expectNoFault('CANOPY track keyboard handoff');
  });
});
