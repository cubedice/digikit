import fs from 'node:fs';
import path from 'node:path';
import { encoder, type Session } from '@digi/test-support';
import { expect, test } from 'vitest';
import { bounded, dspWords, frames, peak, readFloatWav, rpc, word } from '@digi/regression/runtime/fx-live.ts';
import { SINE } from '@digi/regression/runtime/fixtures.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, selectSine, shot, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { record } from '@digi/regression/runtime/artifacts.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import {
  PH,
  advance,
  chooseTracks,
  float,
  mainPage,
  openPages,
  openPrismPage,
  prismPage,
  setPrism,
  tap,
} from './ui-helpers.ts';

/** The loop contract (LP_*, LS_*, LW_*): the one file the home, both pages and both engines share. */
const L: Record<string, number> = Object.fromEntries(
  [
    ...fs
      .readFileSync(new URL('../../../../sdk/dsp/contract/dk_loop.inc', import.meta.url), 'utf8')
      .matchAll(/^#define (L[PSTW]_\w+) (0x[0-9a-fA-F]+|\d+)$/gm),
  ].map((match) => [match[1]!, Number(match[2])]),
);
const BLOCK = 32;
/** 16 steps at 120 BPM. */
const CYCLE = 96000;
/** PRISM's echo: zone SLAP at RATE 0, 60 ms. */
const ECHO = 2880;

// The transition of the plan's section 4, all from PRISM's page, on the patched firmware with the test instrument on two patterns:
// PRISM echoes pattern 1; ARM and the change keep pattern 1 as the loop and PRISM takes it; pattern 2 comes in dry at LIVE; G gives
// PRISM pattern 2; FUNC + NO drops the loop. The rig proves the engines sample for sample (tests/transition.rig.test.ts); this
// proves the firmware's position stream, the control path, both pages and the master stage with them.
describeOn('dt2', 'DTII PRISM and Overloop: a transition from PRISM\'s page', () => {
  const context = withPreparedFxSession('dt2', 'PRISM transition', {
    image: {
      name: 'prism-transition',
      modules: [
        'core/runtime',
        'core/dynreg',
        'core/dyndsp',
        'core/dynfx',
        'core/loop-storage',
        'core/pattern-position',
        'core/loop-display',
        'core/phrase-ui',
        'core/digikit-menu',
        'core/health',
      ],
      inventory: [SINE, 'digikit/overloop', 'digikit/prism'],
      symbols: [
        'DKRT_BOOT',
        'fx.rows',
        'phrase_ui_debug',
        'phrase_ui_values',
        'phrase_ui_page',
        'loop_ui_debug',
        'loop_ui_page',
        'loop_control_raw',
        '_phrase_status',
        '_loop_ui_status',
        '_loop_wet',
        '_loop_buffer_info',
      ],
      policy: 'fx-routes',
    },
    initial: 'pair',
    presets: { pair: { selection: ['modules/m0.dkm', 'modules/m1.dkm', 'modules/m2.dkm'] } },
    requireAudible: true,
  });

  test('ARM and a change on PRISM\'s page: the loop goes on with PRISM, PRISM takes the loop, LIVE brings pattern 2 dry, G turned to LIVE gives it to PRISM, FUNC + NO drops and INPT goes to LIVE', async () => {
    const { fixture, plan, profile, artifacts } = context;
    const runtime = fixture.runtime;
    const session = runtime.session;
    const boot = await inspectFxBoot(runtime, plan, [SINE, 'digikit/overloop', 'digikit/prism'], artifacts, 'boot');
    context.rememberEpoch(boot.epoch);
    const sym = (name: string) => planSymbol(plan, name);
    const values = sym('phrase_ui_values');
    const page = () => prismPage(session, sym('phrase_ui_page'));
    const inpt = () => word(session, values + 4 * PH.PH_P_INPT!);
    const xfad = () => word(session, sym('loop_control_raw') + 4 * L.LP_XFAD!);
    const loop = async () => {
      const w = await dspWords(session, sym('_loop_ui_status'), 16);
      const state = w[L.LS_STATE!]!;
      return {
        loop: (state & L.LS_LOOP!) !== 0,
        armed: (state & L.LS_ARMED!) !== 0,
        count: state >>> L.LS_COUNT_SHIFT!,
        loopFrames: w[L.LS_LOOP_FRAMES!]!,
        phase: w[L.LS_PHASE!]!,
      };
    };
    const prism = async () => {
      const w = await dspWords(session, sym('_phrase_status'), 32);
      return { input: w[PH.PH_S_INPUT! / 4]!, feed: w[PH.PH_S_LOOP! / 4]!, route: w[PH.PH_S_ROUTE! / 4]!, error: w[PH.PH_S_ERROR! / 4]! };
    };
    /** PRISM's output as it went to Overloop in the last block (the wet mailbox), its peak over some reads. */
    const wet = async (reads = 20) => {
      let top = 0;
      for (let i = 0; i < reads; i++) {
        const w = await dspWords(session, sym('_loop_wet'), L.LW_WORDS!);
        for (let k = L.LW_LEFT!; k < L.LW_WORDS!; k++) top = Math.max(top, Math.abs(float(w[k]!)));
        await advance(session, 23);
      }
      return top;
    };
    const turn = async (knob: string, target: number, current: () => Promise<number>) => {
      for (let attempt = 0; attempt < 40 && (await current()) !== target; attempt++) {
        const delta = Math.max(-8, Math.min(8, target - (await current())));
        await bounded(() => session.client.inputTurn(encoder(knob), delta));
        await advance(session, 40);
      }
      expect(await current(), `${knob} reaches ${target}`).toBe(target);
    };
    const prismOnTop = () => openPrismPage(session, sym('phrase_ui_debug'));
    const loopOnTop = async () => {
      await mainPage(session);
      await openPages(session);
      for (let attempt = 0; attempt < 8 && !(await word(session, sym('loop_ui_debug') + 24)); attempt++) await tap(session, 'FX', 300, 'FUNC');
      expect(await word(session, sym('loop_ui_debug') + 24), "Overloop's page is on top").not.toBe(0);
    };
    const loopPageXfad = async () => {
      const bytes = Buffer.alloc(120);
      for (let i = 0; i < 30; i++) bytes.writeUInt32BE(await word(session, sym('loop_ui_page') + 4 * i), 4 * i);
      const end = bytes.indexOf(0, 92);
      return bytes.toString('latin1', 92, Math.min(end < 0 ? 104 : end, 104));
    };
    const program = async (steps: number[]) => {
      await tap(session, 'RECORD', 200);
      for (const step of steps) await tap(session, `TRIG ${step}`, 80);
      await tap(session, 'RECORD', 200);
    };
    const selectPattern = (n: number) => tap(session, `TRIG ${n}`, 400, 'PTN');
    // PRISM hears these tracks: G held and their TRIG keys on PRISM's page (the choice is PRISM's, the same in every pattern)
    const hear = async (tracks: number[]) => {
      await prismOnTop();
      await chooseTracks(session, values, tracks);
    };

    // Pattern 1: the test instrument on track 1 with four trigs; pattern 2: the same instrument on other steps. PRISM hears track 1.
    await selectSine(runtime, profile);
    await mainPage(session);
    await program([1, 5, 9, 13]);
    await selectPattern(2);
    await selectSine(runtime, profile);
    await mainPage(session);
    await program([1, 2, 4, 7, 8, 11, 15, 16]);
    await selectPattern(1);
    await hear([1]);
    // PRISM: one echo of 60 ms with repeats, MIX full
    await setPrism(session, values, PH.PH_P_ZONE!, 'ENC A', 3, { detent: true });
    await setPrism(session, values, PH.PH_P_RATE!, 'ENC B', 0, { coarse: PH.PH_RATE_COARSE });
    await setPrism(session, values, PH.PH_P_RPTS!, 'ENC C', 64);
    await setPrism(session, values, PH.PH_P_MIX!, 'LEVEL', 127);

    await tap(session, 'PLAY', 80);
    await advance(session, 4600);
    const file = path.join(artifacts, 'transition.wav');
    await rpc(session, 'pcm.tap', { action: 'start' });
    await rpc(session, 'pcm.tap', { action: 'capture', path: file });
    const mark = async () => (await frames(session)) * BLOCK;
    const origin = await mark();
    const marks: Record<string, number> = {};
    const report: Record<string, unknown> = {};
    try {
      // 1. Pattern 1 plays and PRISM echoes it from the tracks; while Overloop runs it adds PRISM's output behind its crossfader.
      expect(await prism()).toMatchObject({ input: 0, route: 1, error: 0 });
      report.wetLive = await wet();
      expect(report.wetLive as number, 'PRISM echoes pattern 1').toBeGreaterThan(0.005);
      let p = await page();
      expect(p.cells[6]).toMatchObject({ label: 'INPT', value: 'LIVE' });
      expect(p.cells[7]).toMatchObject({ label: 'XFAD', value: '--', state: 'NO LOOP' });
      await shot(session, artifacts, 'step0-pattern1');
      // press H: ARM
      await tap(session, 'PUSH H', 150);
      await advance(session, 300);
      expect((await loop()).armed).toBe(true);
      p = await page();
      expect(p.cells[7]).toMatchObject({ value: 'ARMD', state: '16 ST' });
      await shot(session, artifacts, 'step1-armed');

      // 2. Pattern 2 the usual way: at the end of the cycle the loop takes over at LOOP, INPT goes to LOOP by itself.
      marks.chosen = await mark();
      await selectPattern(2);
      for (let waited = 0; waited < 3000 && (await loop()).count !== 1; waited += 50) await advance(session, 50);
      expect((await loop()).count, 'the armed change takes a loop').toBe(1);
      // the change itself: the loop's phase counts the frames since it
      marks.change = (await mark()) - (await loop()).phase;
      await advance(session, 300);
      expect(await loop()).toMatchObject({ loop: true, armed: false, loopFrames: CYCLE });
      expect(await inpt(), 'INPT went to LOOP by itself').toBe(1);
      expect(await xfad(), 'the crossfader stands at LOOP').toBe(127);
      expect(await prism()).toMatchObject({ input: 1, feed: 1, route: 1 });
      p = await page();
      expect(p.cells[6]!.value).toBe('LOOP');
      expect(p.cells[7]!.value).toBe('LOOP');
      expect(p.line).toBe('LOOP 16 ST 2.00 S');
      await shot(session, artifacts, 'step2-loop');
      await advance(session, 2200);

      // 3. PRISM plays on the old pattern: a control move changes what is heard; the tracks' sends do not reach it. Track 1 off and on
      // again with G held and TRIG 1: the hold with a TRIG key does not switch INPT.
      marks.move = await mark();
      await setPrism(session, values, PH.PH_P_MIX!, 'LEVEL', 64);
      await advance(session, 2200);
      await shot(session, artifacts, 'step3-play');
      marks.amount0 = await mark();
      await hear([]);
      expect(await inpt(), 'G held with a TRIG key: INPT stays LOOP').toBe(1);
      await advance(session, 2200);
      marks.amount127 = await mark();
      await hear([1]);
      expect(await inpt()).toBe(1);
      await advance(session, 2200);
      marks.amountBack = await mark();

      // 4. H toward LIVE: half way, then LIVE.
      await turn('ENC H', 64, xfad);
      await advance(session, 300);
      p = await page();
      expect(p.cells[7]!.value).toBe('64');
      await shot(session, artifacts, 'step4-half');
      // the same crossfader on Overloop's page, and an ARM from there is seen on PRISM's
      await loopOnTop();
      expect(await loopPageXfad()).toBe('64');
      await tap(session, 'PUSH H', 150);
      await advance(session, 300);
      expect((await loop()).armed).toBe(true);
      await prismOnTop();
      await advance(session, 200);
      expect((await page()).cells[7]).toMatchObject({ state: 'ARMD', lit: true });
      await tap(session, 'PUSH H', 150);
      await advance(session, 300);
      expect((await loop()).armed, 'a second press takes the arm back').toBe(false);
      await turn('ENC H', 0, xfad);
      marks.live = await mark();
      await advance(session, 3000);
      p = await page();
      expect(p.cells[7]!.value).toBe('LIVE');
      expect(await inpt(), 'INPT stays at LOOP: switching it is the performer\'s move').toBe(1);
      report.wetAtLive = await wet();
      expect(report.wetAtLive as number, 'PRISM hears nothing new: the loop\'s feed followed the crossfader').toBeLessThan(1e-4);
      await shot(session, artifacts, 'step5-live');
      marks.liveEnd = await mark();

      // 5. G turned to the left: PRISM takes pattern 2 from the tracks it hears. A press of G switches nothing.
      await tap(session, 'PUSH G', 150);
      await advance(session, 300);
      expect(await inpt(), 'a press of G switches nothing').toBe(1);
      // the stock input path holds an encoder's turns back for about 0.3 s after its press (finding 359)
      await advance(session, 200);
      await turn('ENC G', 0, inpt);
      await advance(session, 300);
      expect(await inpt()).toBe(0);
      expect(await prism()).toMatchObject({ input: 0 });
      p = await page();
      expect(p.cells[6]!.value).toBe('LIVE');
      expect(p.line).toBe('');
      await advance(session, 1500);
      report.wetPattern2 = await wet();
      expect(report.wetPattern2 as number, 'PRISM echoes pattern 2').toBeGreaterThan(0.005);
      await shot(session, artifacts, 'step6-inpt-live');

      // A pattern change that was not armed changes nothing of the loop, INPT or the crossfader.
      await selectPattern(1);
      await advance(session, 2600);
      expect(await loop()).toMatchObject({ loop: true, count: 1 });
      expect(await inpt()).toBe(0);
      expect(await xfad()).toBe(0);

      // 6. G to the right: PRISM hears the loop again. FUNC + NO drops the loop, and INPT goes to LIVE by itself.
      await turn('ENC G', 1, inpt);
      await advance(session, 300);
      expect(await prism()).toMatchObject({ input: 1 });
      await tap(session, 'NO', 150, 'FUNC');
      await advance(session, 400);
      expect(await loop()).toMatchObject({ loop: false });
      expect(await inpt(), 'the drop puts INPT at LIVE').toBe(0);
      p = await page();
      expect(p.cells[7]).toMatchObject({ value: '--', state: 'NO LOOP' });
      await shot(session, artifacts, 'step7-dropped');
      // 7. Without a loop G still turns INPT, and it stays at LOOP: PRISM hears silence, and the page says so. Back to LIVE.
      await turn('ENC G', 1, inpt);
      await advance(session, 1500);
      expect(await inpt(), 'no snap back without a loop').toBe(1);
      expect((await page()).cells[6]).toMatchObject({ value: 'LOOP', state: 'SILENT' });
      await shot(session, artifacts, 'step8-loop-without-a-loop');
      await turn('ENC G', 0, inpt);
      // back at LIVE PRISM echoes pattern 2 again, so that its tape holds sound for the two ways below
      await advance(session, 1500);
      expect(await wet(), 'PRISM echoes pattern 2 again').toBeGreaterThan(0.005);
      marks.end = await mark();
    } finally {
      await rpc(session, 'pcm.tap', { action: 'capture_stop' });
    }

    // The numbers of the capture: each sample against the one a cycle earlier, over stretches between the events.
    const pcm = readFloatWav(file);
    const left = Float32Array.from({ length: Math.floor(pcm.length / 2) }, (_, i) => pcm[2 * i]!);
    const top = peak(left);
    const at = (name: string) => marks[name]! - origin;
    const against = (from: number, to: number) => {
      let worst = 0;
      for (let n = Math.max(from, CYCLE); n < Math.min(to, left.length); n++) worst = Math.max(worst, Math.abs(left[n]! - left[n - CYCLE]!));
      return worst / top;
    };
    // (2) from a cycle before the change to PRISM's first echo of what it took after it: the output is pattern 1 with its echoes
    const change = at('change');
    report.change = change;
    report.throughChange = against(at('chosen'), change + ECHO - 4 * BLOCK);
    // (3) the control move: the output changes against a cycle before
    report.afterMove = against(at('move') + 4800, at('move') + 4800 + CYCLE);
    // (3) track 1 off and on again (its send amount 0 and back to full) while PRISM takes the loop: the output repeats with the loop's cycle
    report.sendsAt0 = against(at('amount0') + CYCLE / 2, at('amount127'));
    report.sendsBack = against(at('amount127') + 4800, at('amountBack'));
    record(artifacts, 'transition-report', { top, marks, ...report });
    // For listening: DIGIKIT_AUDIO=DIR keeps the capture and the times of its events (the run's folder is removed when it passes).
    const keep = process.env.DIGIKIT_AUDIO;
    if (keep) {
      fs.mkdirSync(keep, { recursive: true });
      fs.copyFileSync(file, path.join(keep, 'transition.wav'));
      fs.writeFileSync(path.join(keep, 'transition-marks.json'), JSON.stringify({ origin, marks, report }, null, 2));
    }
    console.info('PRISM transition:', JSON.stringify({ top, ...report }));
    expect(top, 'the capture holds sound').toBeGreaterThan(0.01);
    expect(report.throughChange as number, 'across the change the output goes on as pattern 1 with PRISM').toBeLessThan(0.01);
    expect(report.afterMove as number, 'a PRISM control move is heard').toBeGreaterThan(0.01);
    expect(report.sendsAt0 as number, 'the tracks\' sends do not reach PRISM at LOOP').toBeLessThan(0.01);
    expect(report.sendsBack as number).toBeLessThan(0.01);
    await levelOfTheTwoWays(session, sym, artifacts);
    await bounded(() => session.expectNoFault('PRISM and Overloop: a transition'));
  }, 1_200_000);
});

/**
 * PRISM's output heard through Overloop and through the FX return, at the same level: the transport stopped, PRISM holding its
 * loop (it repeats unchanged), Overloop running at LIVE; then Overloop's lease descriptor is broken by a debug write, so Overloop
 * faults until its adapter switches it off and PRISM goes back to the FX return. The two levels against each other.
 */
async function levelOfTheTwoWays(session: Session, sym: (name: string) => number, artifacts: string): Promise<void> {
  await tap(session, 'STOP', 150);
  await tap(session, 'PUSH C', 150);
  await advance(session, 3000);
  const level = async (tag: string) => {
    const file = path.join(artifacts, `${tag}.wav`);
    await rpc(session, 'pcm.tap', { action: 'start' });
    await rpc(session, 'pcm.tap', { action: 'capture', path: file });
    await advance(session, 600);
    await rpc(session, 'pcm.tap', { action: 'capture_stop' });
    const pcm = readFloatWav(file);
    let sum = 0;
    for (const v of pcm) sum += v * v;
    return Math.sqrt(sum / Math.max(1, pcm.length));
  };
  const throughOverloop = await level('held-through-overloop');
  const status = await dspWords(session, sym('_phrase_status'), 32);
  expect(status[PH.PH_S_ROUTE! / 4], 'Overloop takes PRISM\'s output').toBe(1);
  // a debug write: Overloop's lease descriptor loses its magic word
  await rpc(session, 'dsp.patch', { text: `DM ${sym('_loop_buffer_info').toString(16)} 00000000\n` });
  await advance(session, 1000);
  const after = await dspWords(session, sym('_phrase_status'), 32);
  expect(after[PH.PH_S_ROUTE! / 4], 'PRISM is back on the FX return').toBe(0);
  const throughReturn = await level('held-through-return');
  const ratio = throughOverloop / throughReturn;
  record(artifacts, 'two-ways', { throughOverloop, throughReturn, ratio });
  console.info('PRISM through Overloop and through the FX return:', JSON.stringify({ throughOverloop, throughReturn, ratio }));
  expect(throughReturn).toBeGreaterThan(0.001);
  expect(Math.abs(ratio - 1), 'the two ways are equally loud').toBeLessThan(0.01);
}
