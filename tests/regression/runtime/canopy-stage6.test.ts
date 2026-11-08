import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { repoRoot, withinRuntimeDeadline } from '@digi/test-support';

import { expect, onTestFinished, test } from 'vitest';
import { withSession } from '../support.ts';
import { liveOptions } from './devices.ts';
import { liveProtocol } from './protocol.ts';
import { waitEmulated } from './waits.ts';
import { expectRuntimeUi } from './ready.ts';
import { planSymbol, type RuntimePlan } from './runtime-plan.ts';
import { captureRemoteOled } from './remote.ts';
import { openDigikitMenu } from './runtime-menu.ts';
import { waitInstrumentReady } from './instrument-ready.ts';
import { acknowledgedPanel } from './panel-ack.ts';
import { menuSelection } from './menu-selection.ts';
import { turnAppliedControl } from './encoder-applied.ts';
import { encoderValue } from './encoder-value.ts';
import { canopyControls } from './canopy-controls.ts';
import { analysisJson } from './fx-live.ts';
import { pageKit } from './fx-pages-ui.ts';
import type { FxProfile } from './fx-session.ts';

const h = withSession({ ...liveOptions('dt2'), dsp: 'sharc', restart: false });
test(
  'CANOPY wavetable, four-note arp and shared tails coexist with Overloop and uploads',
  async () => {
    const root = repoRoot(),
      image = path.resolve(root, process.env.CANOPY_COEXIST_IMAGE ?? 'out/patch/canopy-stage6.syx');
    const plan: RuntimePlan = JSON.parse(fs.readFileSync(image.replace(/\.syx$/, '.plan.json'), 'utf8'));
    expect(createHash('sha256').update(fs.readFileSync(image)).digest('hex')).toBe(plan.output?.syx_sha256);
    expect(plan.modules.some((m) => m.startsWith('test/canopy'))).toBe(false);
    const { session } = h;
    await session.client.firmwareLoad({
      syx: image,
      base_sha256: plan.base.syx_sha256,
      start: 'auto',
      force: true,
      auto_no: true,
      card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    const dk = liveProtocol(session),
      file = fs.readFileSync(path.resolve(root, process.env.CANOPY_COEXIST_PACKAGE ?? 'out/patch/canopy-stage6.dkm'));
    await dk.put('modules/canopy.dkm', file);
    expect(await dk.verifyFile('modules/canopy.dkm', file)).toBe(true);
    const loop = fs.readFileSync(
      path.resolve(root, process.env.OVERLOOP_COEXIST_PACKAGE ?? 'out/patch/overloop-stage6.dkm'),
    );
    await dk.put('modules/overloop.dkm', loop);
    expect(await dk.verifyFile('modules/overloop.dkm', loop)).toBe(true);
    await dk.select(['modules/canopy.dkm', 'modules/overloop.dkm']);
    const beforeRestart = (await session.status()).epoch;
    if (typeof beforeRestart !== 'number') throw new Error('Missing pre-restart epoch');
    await dk.restart();
    await dk.waitForBoot(undefined, 180_000);
    const word = async (address: number) => {
      const value = await session.client.memPeek(address);
      if (value === null) throw new Error(`Unreadable firmware word at ${address.toString(16)}`);
      return value;
    };
    const cf = (name: string, i = 0) => word(planSymbol(plan, name) + 4 * i);
    const diagnostic = async () => {
      const values: Record<string, unknown> = {};
      const deadline = Date.now() + 20_000;
      const capture = async (name: string, read: () => Promise<unknown>) => {
        try {
          values[name] = await withinRuntimeDeadline(deadline, read);
        } catch (error) {
          values[name] = { error: String(error) };
        }
      };
      await Promise.all([
        // Queue the compact liveness/DSP reads before the individual CF words.
        capture('status', () => session.status()),
        capture('audio_report', () => session.client.call('audio.report')),
        capture('diagnostics', () => session.client.call('diagnostics')),
        ...(
          [
            ['_ir_rows', 64],
            ['_ir_process_rows', 32],
            ['_iv_result', 32],
            ['_mi_heartbeat', 32],
            ['_iv_input', 80],
            ['_pattern_position_info', 80],
            ['_instrument_reply', 64],
            ['_loop_ui_status', 64],
            ['_fx_diag', 32],
            ['_fx_blk', 4],
            ['fx.rows', 288],
          ] as const
        ).map(([name, len]) =>
          capture(name, () =>
            session.client.call('dsp.peek', {
              addr: planSymbol(plan, name),
              len,
            }),
          ),
        ),
        ...(
          [
            ['DKRT_BOOT', 20],
            ['DKRT_INSTRUMENT_FILE', 4],
            ['DKRT_INSTRUMENT_SESSION', 4],
            ['dl_link', 24],
            ['mt_guard_fault', 5],
            ['registry_guard', 8],
            ['frame_block', 1],
            ['loop_ui_snapshot', 16],
            ['loop_ui_age', 1],
            ['loop_ui_updates', 1],
            ['ip_control', 8],
            ['ip_src_debug', 12],
            ['ip_src_refresh', 8],
            ['src_state', 21],
            ['src_status', 160],
          ] as const
        ).map(([name, n]) => capture(name, () => Promise.all(Array.from({ length: n }, (_, i) => cf(name, i))))),
      ]);
      fs.writeFileSync(path.join(root, 'out/canopy-stage6-state.json'), JSON.stringify(values, null, 2));
      return values;
    };
    try {
      await waitInstrumentReady(cf);
    } catch (error) {
      await diagnostic();
      throw error;
    }

    expect((await dk.modules()).map((m) => m.name)).toContain('digikit/canopy');
    expect(await cf('ip_control', 1)).toBe(0); // routes always start disabled
    const peek = async (addr: number, len: number) => {
      const r = await session.client.call<{ hex: string; unknown: number }>('dsp.peek', { addr, len });
      expect(r.unknown).toBe(0);
      return Buffer.from(r.hex, 'hex');
    };
    const row = await peek(planSymbol(plan, '_ir_rows'), 64);
    expect(row.readUInt32LE(0)).toBe(3);
    const state = row.readUInt32LE(16);
    onTestFinished(async () => {
      // Evidence is independent and best effort: a failed screenshot must neither
      // skip the instance dump nor replace the failure that triggered cleanup.
      const captures = [
        [
          'instance',
          async () => {
            const instance = await withinRuntimeDeadline(Date.now() + 20_000, () =>
              session.client.call('dsp.peek', { addr: state, len: 1184 }),
            ).catch((error: unknown) => ({ error: String(error) }));
            fs.writeFileSync(path.join(root, 'out/canopy-stage6-instance.json'), JSON.stringify(instance));
          },
        ],
        ['state', diagnostic],
        ['OLED', () => captureRemoteOled(session, 'canopy-stage6-finish')],
      ] as const;
      const results = await Promise.allSettled(captures.map(([, capture]) => capture()));
      results.forEach((result, index) => {
        if (result.status === 'rejected')
          console.warn(`CANOPY final ${captures[index]![0]} capture failed:`, result.reason);
      });
    });
    const voices = async () => {
      const b = await peek(state, 192);
      return Array.from({ length: 6 }, (_, i) => ({
        slot: i,
        token: b.readUInt32LE(i * 32),
        gate: b.readUInt32LE(i * 32 + 4),
      }))
        .filter((v) => v.token !== 0 && !(v.gate & 4))
        .sort((a, b) => a.token - b.token);
    };
    const { tap: key, edge, turn } = acknowledgedPanel(session);
    const readyArtifacts = path.join(root, 'out/canopy-stage6-ready');
    fs.mkdirSync(readyArtifacts, { recursive: true });
    await expectRuntimeUi(session, plan, readyArtifacts, {
      afterEpoch: beforeRestart,
      timeout: 180_000,
    });
    const fxProfile = await analysisJson<FxProfile>(
      'tools.patchkit.pk_digikit_fxroutes',
      ['profile', '--device', 'dt2'],
      readyArtifacts,
      'fx-profile',
    );
    const loopKit = pageKit(session, fxProfile);
    // Select the admitted machine using the stock machine chooser.
    await edge('FUNC', true);
    try {
      await key('SRC');
    } finally {
      await edge('FUNC', false);
    }
    for (let i = 0; i < 20; i++) await key('DOWN');
    fs.writeFileSync(
      path.join(root, 'out/canopy-stage6-chooser.json'),
      JSON.stringify(await session.client.framebufferGet({ data: true, hash: true })),
    );
    await key('YES');
    await key('YES');
    await openDigikitMenu(key);
    await key('YES'); // KEYBOARD is the first optional setup row
    await key('DOWN'); // skip the track heading
    await key('YES'); // ENABLE
    await expect.poll(() => cf('ip_control', 1), { timeout: 10_000 }).toBe(1);
    await expect.poll(() => cf('ip_control'), { timeout: 30_000 }).toBe(3);
    for (let i = 0; i < 3; i++) await key('NO');
    expect(await cf('ip_admission', 5)).toBe(await cf('ip_control', 7));
    // Stock receive-port settings are independent of the instrument route.
    // Set the emulator fixture to receive both ports, as in the carrier proofs.
    for (const [addr, value] of [
      [0x429368ca, 3],
      [0x42936872, 0],
      [0x429368d6, 1],
    ] as const) {
      const bytes = Buffer.alloc(4);
      bytes.writeUInt32BE(value);
      await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
    }
    expect((await dk.modules()).map((m) => m.name).sort()).toEqual(['digikit/canopy', 'digikit/overloop']);
    const rawBank = async () => {
      const b = await peek(planSymbol(plan, '_iv_controls'), 128);
      return Array.from({ length: 32 }, (_, i) => b.readUInt32LE(i * 4));
    };
    const toneBank = async () => (await rawBank()).slice(16, 28).map((v) => v >>> 16);
    const controls = canopyControls(cf, rawBank);
    const srcPage = async (page: number) => {
      for (let i = 0; i < 3 && (await cf('ip_src_debug', 1)) !== page; i++) await key('SRC');
      expect(await cf('ip_src_debug', 1)).toBe(page);
    };
    const selectMenu = menuSelection(session, plan, 'core/instrument-play', key);
    const menuRow = (row: number, menu = 'menu') => selectMenu(menu, row);
    await openDigikitMenu(key);
    await key('YES');
    await menuRow(18);
    await key('YES'); // ENGINE, same setup row as the old REED toggle
    await key('DOWN');
    await key('DOWN');
    await key('YES');
    await expect.poll(async () => (await rawBank())[1], { timeout: 15000 }).toBe(2);
    expect((await rawBank())[0]! & 255).toBe(9);
    for (let i = 0; i < 3; i++) await key('NO');
    await key('SRC');
    await srcPage(0);
    // Panel flushes clamp at 30 detents: await each applied step before the next.
    await turnAppliedControl(session, 7, async () => (await rawBank())[21]! & 65535, 64); // equal pad/arp mix
    await srcPage(2);
    await turnAppliedControl(session, 3, async () => (await toneBank())[1]!, 350); // center position 4.50
    await turnAppliedControl(session, 4, async () => (await toneBank())[2]!, 50); // moving held chords
    const wheelRoutes = async () => {
      const b = await peek(state + 264 * 4, 32 * 4);
      return Array.from({ length: 32 }, (_, i) => b.readUInt32LE(i * 4));
    };
    await session.client.call('midi.usb.in', { bytes: [0xb0, 1, 91] });
    await session.client.call('midi.in', { bytes: [0xb0, 1, 37] });
    const expectedWheels = Array.from({ length: 32 }, (_, i) => (i === 0 ? 91 : i === 16 ? 37 : 0));
    await expect.poll(wheelRoutes, { timeout: 30000 }).toEqual(expectedWheels);
    await session.client.call('midi.usb.in', { bytes: [0xb1, 1, 127, 0xb0, 120, 0] });
    await waitEmulated(session, 100);
    expect(await wheelRoutes(), 'unclaimed channel and all-sound-off preserve routed wheels').toEqual(expectedWheels);
    await session.client.call('midi.usb.in', { bytes: [0xb0, 121, 0] });
    expectedWheels[0] = 0;
    await expect.poll(wheelRoutes, { timeout: 30000 }).toEqual(expectedWheels);
    const notes = [48, 55, 60, 64];
    for (const [i, note] of notes.entries())
      await session.client.call(i < 2 ? 'midi.usb.in' : 'midi.in', { bytes: [0x90, note, 100] });
    await expect.poll(async () => (await voices()).length, { timeout: 30000 }).toBe(4);
    let held = await voices();
    await session.client.call('midi.usb.in', { bytes: [0xb0, 1, 91] });
    expectedWheels[0] = 91;
    await expect.poll(wheelRoutes, { timeout: 30000 }).toEqual(expectedWheels);
    expect(await voices(), 'wheel changes do not replace held ownership').toEqual(held);
    const aux = await peek(state + 200 * 4, 32 * 4);
    const heldRoutes = Array.from({ length: 8 }, (_, i) => ({
      token: aux.readUInt32LE(i * 16),
      route: aux.readUInt32LE(i * 16 + 8),
    }))
      .filter((v) => !(v.route & 256) && held.some((h) => h.token === v.token))
      .map((v) => v.route)
      .sort((a, b) => a - b);
    expect(heldRoutes).toEqual([0, 0, 16, 16]);
    const arp = async () => {
      const b = await peek(state + 544, 80);
      return { token: b.readUInt32LE(0), gate: b.readUInt32LE(4), serial: b.readUInt32LE(52) };
    };
    await expect.poll(async () => (await arp()).serial, { timeout: 30000 }).toBeGreaterThan(0);
    const loopWord = (i: number) => cf('loop_ui_snapshot', i);
    const turnUntil = async (enc: number, delta: number, reached: () => Promise<boolean>) => {
      for (let i = 0; i < 40 && !(await reached()); i++) {
        await session.client.inputTurn(enc, delta);
        await waitEmulated(session, 50);
      }
      expect(await reached()).toBe(true);
    };
    const observations: Record<string, unknown>[] = [];
    const counters = async () => {
      const status = await session.status();
      const tiers = status.audio?.dsp_tiers as Record<string, unknown> | undefined;
      const instructions = Number(tiers?.instructions),
        idle = Number(tiers?.idle_skip),
        audioSeconds = Number(tiers?.audio_s);
      expect(Number.isFinite(instructions) && Number.isFinite(idle) && Number.isFinite(audioSeconds)).toBe(true);
      return { instructions, idle, audioSeconds, frames: status.audio?.frames, load: status.load };
    };
    const audio = async (name: string, during?: () => Promise<void>) => {
      const file = path.join(root, `out/canopy-stage6-${name}.f32le`);
      const before = await counters();
      await session.client.call('pcm.tap', { action: 'capture', path: file });
      await session.client.call('pcm.tap', { action: 'start' });
      await during?.();
      await expect
        .poll(
          async () => {
            const r = await session.client.call<{ samples: number; non_finite: number }>('pcm.tap', {
              action: 'report',
            });
            expect(r.non_finite).toBe(0);
            return r.samples;
          },
          { timeout: 60000 },
        )
        .toBeGreaterThanOrEqual(24000);
      await session.client.call('pcm.tap', { action: 'capture_stop' });
      const after = await counters();
      const seconds = after.audioSeconds - before.audioSeconds;
      expect(seconds).toBeGreaterThan(0);
      observations.push({
        name,
        before,
        after,
        dspInstructions: after.instructions - before.instructions,
        dspBusyInstructionsPerAudioSecond:
          (after.instructions - before.instructions - (after.idle - before.idle)) / seconds,
      });
      const raw = fs.readFileSync(file);
      let energy = 0,
        peak = 0;
      const start = Math.max(0, raw.length - 8192 * 8);
      for (let i = 0; i < raw.length; i += 4) {
        const value = raw.readFloatLE(i);
        expect(Number.isFinite(value)).toBe(true);
        if (i >= start) energy += value * value;
        peak = Math.max(peak, Math.abs(value));
      }
      const rms = Math.sqrt(energy / ((raw.length - start) / 4));
      expect(rms).toBeGreaterThan(1e-5);
      expect(peak).toBeLessThan(1);
      return { rms, peak };
    };
    onTestFinished(async () => {
      await session.client.call('pcm.tap', { action: 'capture_stop' });
    });
    const live = await audio('live');
    await captureRemoteOled(session, 'canopy-stage6-wavetable-tone');
    await key('STOP');
    await edge('FUNC', true);
    try {
      await key('FX');
    } finally {
      await edge('FUNC', false);
    }
    for (let i = 0; i < 4; i++) await key('UP');
    for (let i = 0; i < 3; i++) await key('DOWN');
    await expect.poll(() => loopWord(0), { timeout: 15000 }).toBe(0x4f564c31);
    await turnUntil(3, 8, async () => (await loopWord(15)) >= 8);
    await turnUntil(1, 8, async () => (await loopWord(2)) === 3);
    await key('PLAY');
    await expect.poll(() => loopWord(2), { timeout: 15000, interval: 100 }).toBe(1);
    // Real runtime protocol transfer while the DSP records and four notes remain held.
    const payload = Buffer.from(Array.from({ length: 32768 }, (_, i) => (i * 29) & 255));
    await dk.put('stage6/transfer.bin', payload);
    expect(await dk.verifyFile('stage6/transfer.bin', payload)).toBe(true);
    await expect.poll(() => loopWord(2), { timeout: 180000, interval: 200 }).toBe(2);
    await key('STOP');
    expect(await voices()).toEqual(held);
    const frames = await loopWord(3);
    expect(frames).toBeGreaterThan(32);
    await turnUntil(3, -4, async () => (await loopWord(15)) <= 2);
    if (!(await loopWord(15))) await turnUntil(3, 1, async () => (await loopWord(15)) > 0);
    await expect.poll(() => loopWord(13), { timeout: 10000 }).toBeLessThan(frames);
    await turnUntil(2, 1, async () => (await loopWord(14)) > 0);
    await expect.poll(() => loopWord(12), { timeout: 10000 }).toBeGreaterThan(0);
    // Physical D edits logical parameter 2. Native FX controls use a fine raw
    // scale; settle their kit value before waiting for the DSP publication.
    const halfFadeRaw = await loopKit.setRaw('ENC D', 2, 0x4200);
    expect(halfFadeRaw, 'Overloop half fade: raw target 0x4200, minimum 0x4000').toBeGreaterThanOrEqual(0x4000);
    await expect
      .poll(() => loopWord(6), {
        timeout: 30_000,
        message: `Overloop half fade publishes gain >= 0.5 after raw 0x${halfFadeRaw.toString(16)} (target 0x4200)`,
      })
      .toBeGreaterThanOrEqual(0x3f000000);
    await key('SRC');
    await srcPage(2);
    const replacements = [67, 71, 74, 79];
    let tailWitness:
      { tokens: number[]; gates: number[]; arpToken: number; arpGate: number; steps: number } | undefined;
    const transitions = await audio('wavetable-loop-transitions', async () => {
      for (let table = 1; table <= 4; table++) {
        // A long Overloop recording can leave the native encoder idle filter
        // armed. Drain each packet and its configuration before retrying a detent.
        await encoderValue(
          (delta) => turn(2, delta),
          () => controls.settled(16),
          table,
        );
        expect((await toneBank())[0]).toBe(table);
        await waitEmulated(session, 50);
        expect(await voices()).toEqual(held);
      }
      const serial = (await arp()).serial;
      await expect.poll(async () => (await arp()).serial, { timeout: 30000 }).toBeGreaterThan(serial);
      // Shared release tails last at most 64 samples. Observe bounded instruction
      // slices while paused so a wall-time poll cannot skip the entire event.
      // Only normal MIDI input is injected; no voice or scheduler state is edited.
      await session.client.call('run.mode', { mode: 'paused' });
      try {
        await session.client.call('midi.usb.in', { bytes: replacements.flatMap((n) => [0x90, n, 100]), chunk: 12 });
        for (let steps = 1; steps <= 256 && !tailWitness; steps++) {
          await session.client.step(44_000, { timeout_wall_s: 30 });
          const slots = await peek(state, 192),
            a = await arp();
          const tokens = Array.from({ length: 6 }, (_, i) => slots.readUInt32LE(i * 32));
          const gates = Array.from({ length: 6 }, (_, i) => slots.readUInt32LE(i * 32 + 4));
          if (tokens.slice(0, 4).every((token) => token !== 0) && tokens[4] && tokens[5] && a.token) {
            // Pad steals use the bounded reset ramp; an arp release can already
            // occupy either shared tail with its own lane bit.
            expect(gates[4]! & 6).not.toBe(0);
            expect(gates[5]! & 6).not.toBe(0);
            tailWitness = { tokens, gates, arpToken: a.token, arpGate: a.gate, steps };
          }
        }
        expect(tailWitness, 'four-note replacement exercises both shared tails beside the arp').toBeDefined();
      } finally {
        await session.client.call('run.mode', { mode: 'free' });
      }
      await expect
        .poll(async () => (await voices()).filter((v) => v.slot < 4 && v.gate === 1).length, { timeout: 30000 })
        .toBe(4);
      await waitEmulated(session, 100);
      held = await voices();
      expect(held).toHaveLength(4);
      expect(await cf('mt_guard_fault')).toBe(0);
    });
    await edge('FUNC', true);
    try {
      await key('FX');
    } finally {
      await edge('FUNC', false);
    }
    for (let i = 0; i < 4; i++) await key('UP');
    for (let i = 0; i < 3; i++) await key('DOWN');
    await loopKit.maximum('ENC D', 2);
    const fullFadeRaw = await loopKit.raw(2);
    expect(fullFadeRaw, 'Overloop full fade: raw target 0x7f00').toBe(0x7f00);
    await expect
      .poll(() => loopWord(6), {
        timeout: 30_000,
        message: `Overloop full fade publishes loop-side gain after raw 0x${fullFadeRaw.toString(16)} (target 0x7f00)`,
      })
      .toBeGreaterThanOrEqual(0x3f7f0000);
    const loopAudio = await audio('loop');
    for (const note of [...notes, ...replacements]) {
      await session.client.call('midi.usb.in', { bytes: [0x80, note, 0] });
      await session.client.call('midi.in', { bytes: [0x80, note, 0] });
    }
    // RELEASE=15 takes one device second from a sustained amplitude. The
    // ordinary release voices remain present until that envelope reaches zero.
    await waitEmulated(session, 1050);
    await expect.poll(async () => (await voices()).length, { timeout: 30000 }).toBe(0);
    const released = await audio('loop-after-release');
    expect(await dk.verifyFile('stage6/transfer.bin', payload)).toBe(true);
    expect(await cf('mt_guard_fault')).toBe(0);
    expect(await cf('ip_control', 4)).toBe(0);
    await session.expectNoFault('CANOPY wavetable and Overloop with active uploads and shared tails');
    fs.writeFileSync(
      path.join(root, 'out/canopy-stage6-coexistence.json'),
      JSON.stringify(
        {
          live,
          transitions,
          loopAudio,
          released,
          frames,
          held,
          tailWitness,
          wheelRoutes: expectedWheels,
          heldRoutes,
          observations,
          qualification:
            'Emulated integration and raw instruction observations only; neither a paired performance benchmark nor a silicon capacity qualification.',
        },
        null,
        2,
      ),
    );
  },
  45 * 60_000,
);
