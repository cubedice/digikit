import fs from 'node:fs';
import path from 'node:path';
import { encoder, type Session } from '@digi/test-support';
import { expect, test } from 'vitest';
import {
  bounded,
  dspWords,
  frames,
  peak,
  readFloatWav,
  rows,
  rpc,
  word,
  REDRAW,
  SETTLE,
} from '@digi/regression/runtime/fx-live.ts';
import { SINE } from '@digi/regression/runtime/fixtures.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, selectSine, shot, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { mainPage, openPages } from '@digi/regression/runtime/fx-pages-ui.ts';
import { record } from '@digi/regression/runtime/artifacts.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';

/** The contract of the loop effect (LP_*, LS_*), read from the one file the home, the page and the engine share. */
const L: Record<string, number> = Object.fromEntries(
  [
    ...fs
      .readFileSync(new URL('../../../../sdk/dsp/contract/dk_loop.inc', import.meta.url), 'utf8')
      .matchAll(/^#define (L[PST]_\w+) (0x[0-9a-fA-F]+|\d+)$/gm),
  ].map((match) => [match[1]!, Number(match[2])]),
);
/** Audio frames of a DSP block: the emulator's frame count is in blocks. */
const BLOCK = 32;

const float = (bits: number) => {
  const bytes = Buffer.alloc(4);
  bytes.writeUInt32LE(bits >>> 0);
  return bytes.readFloatLE();
};
const tap = (session: Session, key: string, settleMs = 300, held?: string) =>
  sessionRt(session).ui.tap(key, { until: { ms: settleMs, why: REDRAW }, ...(held ? { held } : {}) });
const advance = (session: Session, ms: number) => sessionRt(session).ui.wait({ ms, why: SETTLE });

// An armed pattern change on the patched firmware, with the test instrument on two patterns: what the rig proves about the engine
// (tests/loop.rig.test.ts) is shown here to hold with the real position stream, the real control path and the real page.
describeOn('dt2', 'DTII Overloop: an armed pattern change', () => {
  const context = withPreparedFxSession('dt2', 'Overloop transition', {
    image: {
      name: 'overloop-transition',
      modules: [
        'core/runtime',
        'core/dynreg',
        'core/dyndsp',
        'core/dynfx',
        'core/loop-storage',
        'core/pattern-position',
        'core/loop-display',
        'core/digikit-menu',
        'core/health',
      ],
      inventory: [SINE, 'digikit/overloop'],
      symbols: [
        'DKRT_BOOT',
        'fx.rows',
        'loop_ui_debug',
        'loop_ui_values',
        'loop_control_raw',
        'loop_ui_page',
        '_loop_ui_status',
        '_loop_buffer_info',
        '_phrase_storage_info',
        '_pattern_position_info',
      ],
      policy: 'settings',
    },
    initial: 'loop',
    presets: { loop: { selection: ['modules/m0.dkm', 'modules/m1.dkm'] } },
    requireAudible: true,
  });

  test('ARM, a change from pattern 1 to pattern 2, the loop, the crossfader to LIVE; then a cycle longer than the buffers', async () => {
    const { fixture, plan, profile, artifacts } = context;
    const runtime = fixture.runtime;
    const session = runtime.session;
    const boot = await inspectFxBoot(runtime, plan, [SINE, 'digikit/overloop'], artifacts, 'boot');
    context.rememberEpoch(boot.epoch);
    const debug = planSymbol(plan, 'loop_ui_debug');
    const cf = (name: string, index = 0) => word(session, planSymbol(plan, name) + 4 * index);
    const status = async () => {
      const w = await dspWords(session, planSymbol(plan, '_loop_ui_status'), 16);
      const state = w[L.LS_STATE!]!;
      return {
        heartbeat: w[L.LS_HEARTBEAT!]!,
        loop: (state & L.LS_LOOP!) !== 0,
        armed: (state & L.LS_ARMED!) !== 0,
        cut: (state & L.LS_CUT!) !== 0,
        track: (state & L.LS_TRACK!) !== 0,
        nextCut: (state & L.LS_NEXT_CUT!) !== 0,
        count: state >>> L.LS_COUNT_SHIFT!,
        loopFrames: w[L.LS_LOOP_FRAMES!]!,
        nextFrames: w[L.LS_NEXT_FRAMES!]!,
        gain: float(w[L.LS_GAIN!]!),
        loopSteps: w[L.LS_STEPS!]! & 255,
        steps: (w[L.LS_STEPS!]! >>> 8) & 255,
        capacity: w[L.LS_CAPACITY!]!,
      };
    };
    /** The page as core/loop-display last painted it (`loop_ui_page`, the `LoopPage` of its model.h). */
    const page = async () => {
      const bytes = Buffer.alloc(120);
      for (let i = 0; i < 30; i++) bytes.writeUInt32BE(await cf('loop_ui_page', i), 4 * i);
      const text = (at: number, field: number) => {
        const end = bytes.indexOf(0, at);
        return bytes.toString('latin1', at, Math.min(end < 0 ? at + field : end, at + field));
      };
      return {
        title: text(0, 12),
        state: text(12, 12),
        line1: text(24, 28),
        line2: text(52, 28),
        arm: text(80, 12),
        xfad: text(92, 12),
        armed: bytes.readUInt32BE(104) !== 0,
        loop: bytes.readUInt32BE(108) !== 0,
        mark: bytes.readUInt32BE(112),
        fader: bytes.readUInt32BE(116),
      };
    };
    // the home's crossfader (core/loop-control), not the page's copy of it, which is a callback old: a turn read against the
    // copy can be sent twice
    const xfad = () => cf('loop_control_raw', L.LP_XFAD!);
    const setXfad = async (target: number) => {
      for (let attempt = 0; attempt < 40; attempt++) {
        const error = target - (await xfad());
        if (!error) return;
        await bounded(() => session.client.inputTurn(encoder('ENC H'), Math.max(-8, Math.min(8, error))));
        await advance(session, 40);
      }
      expect(await xfad(), `the crossfader reaches ${target}`).toBe(target);
    };
    const openLoopPage = async () => {
      await mainPage(sessionRt(session));
      await openPages(sessionRt(session));
      for (let attempt = 0; attempt < 8 && !(await word(session, debug + 24)); attempt++) await tap(session, 'FX', 300, 'FUNC');
      expect(await word(session, debug + 24), "Overloop's page is on top").not.toBe(0);
    };
    /** Trigs on the given steps of the pattern that is selected, for track 1 (grid recording). */
    const program = async (steps: number[]) => {
      await tap(session, 'RECORD', 200);
      for (const step of steps) await tap(session, `TRIG ${step}`, 80);
      await tap(session, 'RECORD', 200);
    };
    const selectPattern = (n: number) => tap(session, `TRIG ${n}`, 400, 'PTN');
    /** Steps the emulator 50 ms at a time until the engine has counted loop `count` (the pattern change), at most `ms`. */
    const untilCount = async (count: number, ms: number) => {
      for (let waited = 0; waited < ms && (await status()).count !== count; waited += 50) await advance(session, 50);
      expect((await status()).count, `loop ${count} is taken within ${ms} ms`).toBe(count);
    };

    // Pattern 1: the test instrument on track 1 with four trigs; pattern 2: the same instrument with trigs on other steps.
    await selectSine(runtime, profile);
    await mainPage(sessionRt(session));
    await program([1, 5, 9, 13]);
    await selectPattern(2);
    await selectSine(runtime, profile);
    await mainPage(sessionRt(session));
    await program([1, 2, 4, 7, 8, 11, 15, 16]);
    await selectPattern(1);

    // The page before anything plays.
    await openLoopPage();
    await advance(session, 500);
    const idle = await page();
    record(artifacts, 'page-idle', idle);
    await shot(session, artifacts, 'idle');
    expect(idle).toMatchObject({ title: 'OVERLOOP', state: 'IDLE', line1: 'NO LOOP', arm: 'OFF', xfad: 'LIVE', fader: 0 });
    expect((await rows(session, plan, context.slots))[0]!.slice(17, 19), 'the effect runs on the master with no fault').toEqual([0, 0]);

    // Pattern 1 plays; after its first whole cycle the engine knows what a change would keep.
    await tap(session, 'PLAY', 80);
    await advance(session, 4600);
    const playing = await status();
    record(artifacts, 'status-playing', playing);
    expect(playing).toMatchObject({ track: true, loop: false, armed: false, steps: 16 });
    const cycle = playing.nextFrames;
    expect(Math.abs(cycle - 96000), '16 steps at 120 BPM, to a block').toBeLessThanOrEqual(BLOCK);
    expect((await page()).line2).toBe('CYCLE 16 ST 2.00 S');

    // From here the main output is captured.
    const file = path.join(artifacts, 'transition.wav');
    await rpc(session, 'pcm.tap', { action: 'start' });
    await rpc(session, 'pcm.tap', { action: 'capture', path: file });
    const mark = async () => (await frames(session)) * BLOCK;
    const origin = await mark();
    let changeSeen = 0;
    let moved = 0;
    let live = 0;
    let end = 0;
    try {
      // ARM with the press of E, under its label: one-shot, shown on the page.
      await tap(session, 'PUSH E', 150);
      await advance(session, 300);
      expect(await status()).toMatchObject({ armed: true, loop: false });
      const armed = await page();
      record(artifacts, 'page-armed', armed);
      await shot(session, artifacts, 'armed');
      expect(armed).toMatchObject({ state: 'ARMED', line1: 'NEXT CHANGE KEEPS', line2: '16 ST 2.00 S', arm: 'ON', armed: true });

      // Pattern 2 is chosen the usual way: it comes at the end of pattern 1's cycle, and the loop takes over there.
      await selectPattern(2);
      await untilCount(1, 3000);
      changeSeen = await mark();
      await advance(session, 4400);
      const looping = await status();
      record(artifacts, 'status-loop', looping);
      expect(looping).toMatchObject({ loop: true, armed: false, cut: false, count: 1, loopSteps: 16, gain: 1 });
      expect(looping.loopFrames, 'the loop is the cycle that was heard').toBe(cycle);
      expect(await xfad(), 'the home has set the crossfader to LOOP').toBe(127);
      const atLoop = await page();
      record(artifacts, 'page-loop', atLoop);
      await shot(session, artifacts, 'loop');
      expect(atLoop).toMatchObject({ state: 'LOOP', line1: 'LOOP 16 ST 2.00 S', arm: 'OFF', xfad: 'LOOP', fader: 127, loop: true });

      // The crossfader half way, then at LIVE: pattern 2 comes in.
      moved = await mark();
      await setXfad(64);
      await advance(session, 400);
      const half = await page();
      record(artifacts, 'page-half', half);
      await shot(session, artifacts, 'half');
      expect(half.xfad).toBe('64');
      expect(Math.abs(half.fader - 64)).toBeLessThanOrEqual(1);
      expect((await status()).gain).toBeCloseTo(64 / 127, 5);
      await setXfad(0);
      await advance(session, 400);
      live = await mark();
      expect(await status()).toMatchObject({ loop: true, gain: 0, count: 1 });
      const atLive = await page();
      record(artifacts, 'page-live', atLive);
      await shot(session, artifacts, 'live');
      expect(atLive).toMatchObject({ state: 'LOOP', xfad: 'LIVE', fader: 0 });
      await advance(session, 4400);
      end = await mark();
    } finally {
      await rpc(session, 'pcm.tap', { action: 'capture_stop' });
    }
    await advance(session, 100);

    // The numbers of the capture. x[n] against x[n - cycle]: what is heard against one cycle earlier.
    const pcm = readFloatWav(file);
    const left = Float32Array.from({ length: Math.floor(pcm.length / 2) }, (_, i) => pcm[2 * i]!);
    const top = peak(left);
    const against = (from: number, to: number) => {
      let worst = 0;
      let sum = 0;
      for (let n = Math.max(from, cycle); n < Math.min(to, left.length); n++) {
        const d = Math.abs(left[n]! - left[n - cycle]!);
        worst = Math.max(worst, d);
        sum += d * d;
      }
      return { worst: worst / top, rms: Math.sqrt(sum / Math.max(1, Math.min(to, left.length) - Math.max(from, cycle))) / top };
    };
    // pattern 1, the change and the loop, up to the crossfader's move (less a margin for where the marks fall)
    const through = against(0, moved - origin - 4000);
    // the cycle after the crossfader has reached LIVE: pattern 2 against what was heard a cycle before it, the loop of pattern 1
    const arriving = against(live - origin, live - origin + cycle - 4000);
    // pattern 2 alone, a cycle later: it repeats, as a pattern does
    const alone = against(live - origin + cycle + 4000, end - origin - 4000);
    const report = {
      frames: left.length,
      peak: top,
      cycle,
      changeSeenAt: changeSeen - origin,
      loopTurnsBeforeTheMove: (moved - changeSeen) / cycle,
      through,
      arriving,
      alone,
    };
    record(artifacts, 'transition-report', report);
    console.info('Overloop transition:', JSON.stringify(report));
    expect(top, 'the capture holds sound').toBeGreaterThan(0.01);
    expect(report.loopTurnsBeforeTheMove, 'the capture spans the change and two turns of the loop').toBeGreaterThan(2);
    expect(through.worst, 'across the change and the loop the output is what was heard one cycle earlier').toBeLessThan(0.01);
    expect(arriving.worst, 'at LIVE another pattern is heard').toBeGreaterThan(0.2);
    expect(alone.worst, 'and it is a pattern: it repeats').toBeLessThan(0.01);

    // The crossfader and the armed state are not kit values: a pattern change leaves them as they are. First a change that
    // takes no loop because the transport is stopped (the arm stays), then one while it plays and nothing is armed.
    // E turned to the right arms, and turned further stays armed.
    const turnE = (ticks: number) => bounded(() => session.client.inputTurn(encoder('ENC E'), ticks));
    await tap(session, 'STOP', 150);
    await setXfad(50);
    await turnE(1);
    await advance(session, 300);
    await turnE(2);
    await advance(session, 300);
    expect(await status()).toMatchObject({ armed: true, loop: true, count: 1 });
    await selectPattern(1);
    await advance(session, 1500);
    expect(await status(), 'a change while stopped takes no loop and keeps the arm').toMatchObject({ armed: true, loop: true, count: 1, loopFrames: cycle });
    expect(await xfad()).toBe(50);
    const both = await page();
    record(artifacts, 'page-loop-armed', both);
    expect(both).toMatchObject({ state: 'LOOP+ARM', arm: 'ON', xfad: '50' });
    await turnE(-1);
    await advance(session, 300);
    expect((await status()).armed, 'E turned to the left takes the arm back').toBe(false);
    await turnE(-2);
    await advance(session, 300);
    expect((await status()).armed, 'and turned further leaves it off').toBe(false);
    await tap(session, 'PLAY', 80);
    await advance(session, 600);
    await selectPattern(2);
    await advance(session, 3200);
    const after = await status();
    expect(after, 'a change that was not armed leaves the loop and the crossfader').toMatchObject({ armed: false, loop: true, count: 1, loopFrames: cycle });
    expect(after.gain).toBeCloseTo(50 / 127, 5);
    expect(await xfad()).toBe(50);

    // FUNC + NO drops the loop.
    await tap(session, 'NO', 150, 'FUNC');
    await advance(session, 400);
    expect(await status()).toMatchObject({ loop: false, loopFrames: 0, gain: 0 });
    expect(await word(session, debug + 24), 'FUNC + NO is taken by the page').not.toBe(0);
    expect((await page()).line1).toBe('NO LOOP');

    // A cycle longer than the buffers. The device's buffers hold 21.5 s; so that a cycle of 2 s is too long for them, the
    // frames of the second buffer's descriptor are set to 65536 by a debug write (the engine takes what both buffers hold
    // and starts anew when a descriptor changes).
    const descriptor = planSymbol(plan, '_phrase_storage_info');
    await rpc(session, 'dsp.patch', { text: `DM ${(descriptor + 20).toString(16)} ${(65536).toString(16).padStart(8, '0')}\n` });
    await advance(session, 4600);
    const small = await status();
    record(artifacts, 'status-small-buffers', small);
    expect(small).toMatchObject({ track: true, nextCut: true, loop: false, capacity: 65536 - 480 - BLOCK });
    // the press of H arms as well, as on PRISM's page
    await tap(session, 'PUSH H', 150);
    await advance(session, 300);
    const warned = await page();
    record(artifacts, 'page-armed-cut', warned);
    await shot(session, artifacts, 'armed-cut');
    expect(warned).toMatchObject({ state: 'ARMED', line1: 'NEXT CHANGE KEEPS', line2: 'LAST 1.36 S ONLY' });
    const before = (await status()).count;
    await selectPattern(1);
    await untilCount(before + 1, 3000);
    await advance(session, 600);
    const cut = await status();
    record(artifacts, 'status-cut', cut);
    expect(cut).toMatchObject({ loop: true, cut: true, loopFrames: small.capacity, loopSteps: 16 });
    const cutPage = await page();
    record(artifacts, 'page-cut', cutPage);
    await shot(session, artifacts, 'cut');
    expect(cutPage).toMatchObject({ state: 'LOOP', line1: 'LOOP 16 ST 1.36 S', line2: 'CUT TO ITS LAST PART' });
    await tap(session, 'STOP', 150);
    const row = (await rows(session, plan, context.slots))[0]!;
    expect(row[12], 'the effect ran').toBe(1);
    await bounded(() => session.expectNoFault('Overloop: an armed pattern change'));
  }, 900_000);
});
