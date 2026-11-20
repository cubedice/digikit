import fs from 'node:fs';
import path from 'node:path';
import { button, type Session } from '@digi/test-support';
import { expect, test } from 'vitest';
import { bounded, dspWords, frames, readFloatWav, rpc, word, REDRAW, SETTLE } from '@digi/regression/runtime/fx-live.ts';
import { SINE } from '@digi/regression/runtime/fixtures.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { portAnchorAddress } from '@digi/regression/runtime/device-data.ts';
import { inspectFxBoot, selectSine, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
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
/** The loop's seam and the loop side's fade at STOP and START, in frames (src/overloop.h). */
const SEAM = 480;
const FADE = 256;
/** A step of 16ths at 120 BPM, in frames. */
const STEP = 6000;
/** The stock sequencer's scheduled-pattern pointer (finding 133; core/pattern-position's pp_current_pattern). */
const SCHEDULED = 0x4316501c;

const tap = (session: Session, key: string, settleMs = 300, held?: string) =>
  sessionRt(session).ui.tap(key, { until: { ms: settleMs, why: REDRAW }, ...(held ? { held } : {}) });
const advance = (session: Session, ms: number) => sessionRt(session).ui.wait({ ms, why: SETTLE });
/** Track 1's sound played live: TRIG 1 down or up, outside grid recording. */
const key = (session: Session, down: boolean) => bounded(() => session.client.inputButton(button('TRIG 1'), down));

// What the owner heard on the Digitakt II (finding 365): a queued pattern took the loop too soon, and a held loop played on
// through STOP. The firmware's two pattern words are read while a change is queued, and the output shows where the loop
// takes over; then STOP and START with the loop at LOOP.
describeOn('dt2', 'DTII Overloop: a queued change and the transport', () => {
  const context = withPreparedFxSession('dt2', 'Overloop transport', {
    image: {
      name: 'overloop-transport',
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
        'loop_control_raw',
        '_loop_ui_status',
        '_loop_tap',
        '_pattern_position_info',
      ],
      policy: 'settings',
    },
    initial: 'loop',
    presets: { loop: { selection: ['modules/m0.dkm', 'modules/m1.dkm'] } },
    requireAudible: true,
  });

  test('the loop is taken when the queued pattern is heard, and is silent while the transport is stopped', async () => {
    const { fixture, plan, profile, artifacts } = context;
    const runtime = fixture.runtime;
    const session = runtime.session;
    const boot = await inspectFxBoot(runtime, plan, [SINE, 'digikit/overloop'], artifacts, 'boot');
    context.rememberEpoch(boot.epoch);
    const debug = planSymbol(plan, 'loop_ui_debug');
    const status = async () => {
      const w = await dspWords(session, planSymbol(plan, '_loop_ui_status'), 16);
      const state = w[L.LS_STATE!]!;
      return {
        loop: (state & L.LS_LOOP!) !== 0,
        armed: (state & L.LS_ARMED!) !== 0,
        stopped: (state & L.LS_STOPPED!) !== 0,
        count: state >>> L.LS_COUNT_SHIFT!,
        loopFrames: w[L.LS_LOOP_FRAMES!]!,
        nextFrames: w[L.LS_NEXT_FRAMES!]!,
      };
    };
    const xfad = () => word(session, planSymbol(plan, 'loop_control_raw') + 4 * L.LP_XFAD!);
    // the pattern each track's current step belongs to: the literal of the stock tick's `lea playing,a6` (the anchor
    // core/pattern-position reads), track 1's word first
    const playingAt = await word(session, portAnchorAddress('dt2', '1.17', 'cf.pattern.site.playing_lit'));
    const position = planSymbol(plan, '_pattern_position_info');
    const program = async (steps: number[]) => {
      await tap(session, 'RECORD', 200);
      for (const step of steps) await tap(session, `TRIG ${step}`, 80);
      await tap(session, 'RECORD', 200);
    };
    const selectPattern = (n: number) => tap(session, `TRIG ${n}`, 400, 'PTN');
    const mark = async () => (await frames(session)) * BLOCK;

    // Pattern 1: the test instrument on track 1 with four trigs; pattern 2: the same instrument on other steps.
    await selectSine(runtime, profile);
    await mainPage(sessionRt(session));
    await program([1, 5, 9, 13]);
    await selectPattern(2);
    await selectSine(runtime, profile);
    await mainPage(sessionRt(session));
    await program([1, 2, 4, 7, 8, 11, 15, 16]);
    await selectPattern(1);
    await mainPage(sessionRt(session));
    await openPages(sessionRt(session));
    for (let attempt = 0; attempt < 8 && !(await word(session, debug + 24)); attempt++) await tap(session, 'FX', 300, 'FUNC');
    expect(await word(session, debug + 24), "Overloop's page is on top").not.toBe(0);

    await tap(session, 'PLAY', 80);
    await advance(session, 4600);
    const cycle = (await status()).nextFrames;
    expect(Math.abs(cycle - 16 * STEP), '16 steps at 120 BPM, to a block').toBeLessThanOrEqual(BLOCK);

    const file = path.join(artifacts, 'transport.wav');
    await rpc(session, 'pcm.tap', { action: 'start' });
    await rpc(session, 'pcm.tap', { action: 'capture', path: file });
    const origin = await mark();
    type Sample = { frame: number; scheduled: number; playing: number; identity: number; step: number; count: number };
    const take = async (): Promise<Sample> => {
      const rec = await dspWords(session, position, 9);
      return {
        frame: await mark(),
        scheduled: await word(session, SCHEDULED),
        playing: await word(session, playingAt),
        identity: rec[6]!,
        step: rec[7]!,
        count: (await status()).count,
      };
    };
    const samples: Sample[] = [];
    let before: Sample | undefined;
    let keysFrom = 0;
    let stop = 0;
    let silentTo = 0;
    let keysTo = 0;
    let start = 0;
    let end = 0;
    // PAUSE (PLAY while it plays) and CONTINUE (PLAY again): the loop goes on where it stood, in time with the pattern.
    // In time: the loop's phase is within the step the record says plays (the loop began with the pattern at START).
    const where = async () => {
      const rec = await dspWords(session, position, 15);
      const st = await dspWords(session, planSymbol(plan, '_loop_ui_status'), 16);
      const phase = st[L.LS_PHASE!]!;
      const within = (((phase - rec[7]! * STEP) % cycle) + cycle) % cycle;
      return {
        frame: await mark(),
        boundary: rec[3]!,
        generation: rec[4]!,
        flags: rec[5]!,
        step: rec[7]!,
        transport: rec[14]!,
        stopped: (st[L.LS_STATE!]! & L.LS_STOPPED!) !== 0,
        phase,
        within,
      };
    };
    const inStep = (w: { within: number }) => w.within <= STEP + 128 || w.within >= cycle - 128;
    const running: Awaited<ReturnType<typeof where>>[] = [];
    const pausing: Awaited<ReturnType<typeof where>>[] = [];
    const resumed: Awaited<ReturnType<typeof where>>[] = [];
    let pausedAt = 0;
    let continuedAt = 0;
    let resumedStatus: Awaited<ReturnType<typeof status>> | undefined;
    try {
      await tap(session, 'PUSH E', 150);
      await advance(session, 200);
      expect(await status()).toMatchObject({ armed: true, loop: false });
      // a cycle in the capture before the change, then early in a cycle of pattern 1 (its step 2 to 4) pattern 2 is queued
      // the usual way
      await advance(session, 2000);
      for (let waited = 0; waited < 3000; waited += 20) {
        const step = (await dspWords(session, position, 9))[7]!;
        if (step >= 1 && step <= 3) break;
        await advance(session, 20);
      }
      before = await take();
      expect(before.scheduled, 'pattern 1 is the scheduled and the playing pattern').toBe(before.playing);
      await tap(session, 'TRIG 2', 40, 'PTN');
      // and then a live change, which no earlier cycle holds: track 1 played on its key, again and again
      keysFrom = await mark();
      for (let n = 0, waited = 0; waited < 3200; waited += 20, n += 1) {
        if (n % 5 === 0) await key(session, true);
        if (n % 5 === 3) await key(session, false);
        samples.push(await take());
        if (samples.filter((s) => s.count === 1).length > 10) break;
        await advance(session, 20);
      }
      await key(session, false);
      record(artifacts, 'queued-samples', { before, samples });
      await advance(session, 4400);
      const looping = await status();
      expect(looping).toMatchObject({ loop: true, armed: false, count: 1 });
      expect(looping.loopFrames, 'the loop is the cycle').toBe(cycle);
      expect(await xfad(), 'the home has set the crossfader to LOOP').toBe(127);

      // STOP with the loop at LOOP: nothing of the loop is heard (the pattern's last notes ring out), the keys are
      await tap(session, 'STOP', 80);
      stop = await mark();
      await advance(session, 1500);
      expect(await status(), 'stopped: the loop and XFAD are kept').toMatchObject({ loop: true, stopped: true, count: 1 });
      expect(await xfad()).toBe(127);
      // the tap carries no loop: PRISM at LOOP hears silence
      const tapWords = await dspWords(session, planSymbol(plan, '_loop_tap'), L.LT_WORDS!);
      expect(tapWords[L.LT_FLAGS!]! & L.LT_AUDIBLE!, 'the tap says no loop is mixed').toBe(0);
      expect(tapWords.slice(L.LT_LEFT!).every((w) => w === 0), 'and its samples are 0').toBe(true);
      silentTo = await mark();
      await key(session, true);
      await advance(session, 400);
      await key(session, false);
      keysTo = await mark();
      await advance(session, 1000);
      // START: the loop begins again at its first frame, at LOOP
      start = await mark();
      await tap(session, 'PLAY', 80);
      await advance(session, 2400);
      expect(await status()).toMatchObject({ loop: true, stopped: false, count: 1 });
      end = await mark();

      // PAUSE and CONTINUE, with the loop at LOOP
      for (let i = 0; i < 20; i++) {
        running.push(await where());
        await advance(session, 40);
      }
      await tap(session, 'PLAY', 80);
      pausedAt = await mark();
      for (let i = 0; i < 20; i++) {
        pausing.push(await where());
        await advance(session, 40);
      }
      await tap(session, 'PLAY', 80);
      continuedAt = await mark();
      for (let i = 0; i < 60; i++) {
        resumed.push(await where());
        await advance(session, 20);
      }
      resumedStatus = await status();
    } finally {
      await rpc(session, 'pcm.tap', { action: 'capture_stop' });
    }
    await advance(session, 100);

    // The two words while the change was queued: the scheduled pattern moves to pattern 2 a step before the playing one.
    const changed = (pick: (s: Sample) => number) => {
      const at = samples.find((s) => pick(s) !== pick(before!));
      expect(at, 'the word changes after the change is queued').toBeDefined();
      return at!.frame;
    };
    const scheduledAt = changed((s) => s.scheduled);
    const playingChangedAt = changed((s) => s.playing);
    const identityAt = changed((s) => s.identity);
    const takenAt = samples.find((s) => s.count === 1)!.frame;
    const sample = 20 * 48 + 2 * BLOCK;

    // The output: x[n] against x[n - cycle]. The loop takes over where they are the same for a cycle (less the seam, which
    // runs into frames from before the keys); until then the keys are heard.
    const pcm = readFloatWav(file);
    const left = Float32Array.from({ length: Math.floor(pcm.length / 2) }, (_, i) => pcm[2 * i]!);
    const top = left.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
    const same = (a: number, b: number, length: number) => {
      for (let k = 0; k < length; k++) if (Math.abs(left[a + k]! - left[b + k]!) > 1e-6 * top) return false;
      return true;
    };
    let cut = -1;
    for (let n = keysFrom - origin; n + cycle < left.length; n += BLOCK)
      if (n >= cycle && same(n, n - cycle, cycle - SEAM - BLOCK)) {
        cut = n;
        break;
      }
    // after STOP: once the loop side has faded out only the pattern's last notes ring out, so no 50 ms stretch is louder
    // than the one before it (the loop has a note every 4 steps, 0.5 s); then the keys
    const peakIn = (from: number, to: number) => left.slice(from - origin, to - origin).reduce((m, v) => Math.max(m, Math.abs(v)), 0);
    const windows: number[] = [];
    for (let at = stop + 14400; at + 2400 <= silentTo; at += 2400) windows.push(peakIn(at, at + 2400) / top);
    const rise = Math.max(...windows.slice(1).map((p, i) => p - windows[i]!));
    const silence = peakIn(silentTo - 2400, silentTo);
    const keys = peakIn(silentTo, keysTo);
    // after START: the loop from its first frame (the first turn after the change), once it has faded in
    let restart = -1;
    for (let n = start - origin; n < start - origin + 24000 && cut >= 0; n += BLOCK)
      if (same(n + FADE, cut + FADE, 2 * STEP)) {
        restart = n;
        break;
      }
    const report = {
      cycle,
      step: STEP,
      scheduledChangedAt: scheduledAt - origin,
      playingChangedAt: playingChangedAt - origin,
      recordIdentityChangedAt: identityAt - origin,
      loopTakenBy: takenAt - origin,
      cut,
      afterStopWindows: windows.map((p) => Number(p.toFixed(4))),
      afterStopLargestRise: rise,
      ringingOutAfterStop: silence / top,
      keysAfterStop: keys / top,
      restartAfterPlay: restart - (start - origin),
      restart,
      end: end - origin,
      pausedAt: pausedAt - origin,
      continuedAt: continuedAt - origin,
    };
    record(artifacts, 'transport-report', report);
    record(artifacts, 'continue-samples', { running, pausing, resumed });
    console.info('Overloop transport:', JSON.stringify(report));
    // the scheduled pointer moves a step before the pattern that plays (finding 365)
    expect(Math.abs(playingChangedAt - scheduledAt - STEP), 'the scheduled pattern a step early').toBeLessThanOrEqual(2 * sample);
    // the record carries the playing pattern, and the loop is taken with it
    expect(Math.abs(identityAt - playingChangedAt)).toBeLessThanOrEqual(2 * sample);
    expect(Math.abs(takenAt - playingChangedAt)).toBeLessThanOrEqual(2 * sample);
    // the output: the keys are heard up to the switch; the loop takes over there, not a step before
    expect(cut, 'the loop takes over').toBeGreaterThan(0);
    expect(Math.abs(origin + cut - playingChangedAt), 'where the playing pattern changes').toBeLessThanOrEqual(2 * sample);
    expect(origin + cut - scheduledAt, 'not where the scheduled pattern changes').toBeGreaterThan(STEP - 2 * sample);
    // STOP: silence from the loop, the keys are heard; START: the loop from its first frame
    expect(windows.length, 'the stretch after STOP is measured').toBeGreaterThan(10);
    expect(rise, 'after STOP no note starts: the loop is silent').toBeLessThanOrEqual(0.002);
    expect(report.keysAfterStop, 'the keys are heard at LOOP while stopped').toBeGreaterThan(2 * report.ringingOutAfterStop + 0.05);
    expect(restart, 'START: the loop from its first frame').toBeGreaterThan(0);
    expect(restart - (start - origin), 'soon after PLAY').toBeLessThan(12000);
    // PAUSE: the loop side is silent; CONTINUE: a new generation, and the loop goes on in time with the pattern
    expect(running.every(inStep), 'before the pause the loop is in time with the pattern').toBe(true);
    expect(pausing.at(-1), 'PLAY while it plays pauses (raw transport 2)').toMatchObject({ transport: 2, stopped: true });
    const after = resumed.filter((w) => w.generation !== pausing.at(-1)!.generation && (w.flags & 3) === 3);
    expect(after.length, 'CONTINUE starts a new generation').toBeGreaterThan(40);
    expect(after.every(inStep), 'after CONTINUE the loop goes on in time with the pattern').toBe(true);
    expect(resumedStatus).toMatchObject({ loop: true, stopped: false, count: 1 });
    await tap(session, 'STOP', 150);
    expect(await xfad(), 'XFAD is kept').toBe(127);
    await bounded(() => session.expectNoFault('Overloop: a queued change and the transport'));
  }, 900_000);
});
