import { button, encoder } from '@digi/test-support';
import { expect, test } from 'vitest';
import { bounded, dspWords, word } from '@digi/regression/runtime/fx-live.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, shot, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { record } from '@digi/regression/runtime/artifacts.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { PH, advance, float, mainPage, openPrismPage, prismPage, setPrism, tap } from './ui-helpers.ts';

// The one page of PRISM on the real firmware view: what the host harness of core/phrase-ui proves about the model (encoder to value, the
// presses, the texts) is shown here to reach the provider's control words and the painted page through the native key and encoder events.
describeOn('dt2', 'DTII PRISM native page', () => {
  const context = withPreparedFxSession('dt2', 'PRISM UI', {
    image: {
      name: 'prism-ui',
      modules: [
        'core/runtime',
        'core/dynreg',
        'core/dyndsp',
        'core/dynfx',
        'core/phrase-ui',
        'core/digikit-menu',
        'core/health',
      ],
      inventory: ['digikit/prism'],
      symbols: [
        'DKRT_BOOT',
        'fx.rows',
        'phrase_ui_debug',
        'phrase_ui_values',
        'phrase_ui_binding',
        'phrase_ui_page',
        '_phrase_control_info',
        '_phrase_status',
      ],
      policy: 'settings',
    },
    initial: 'phrase',
    presets: { phrase: { selection: ['modules/m0.dkm'] } },
    requireAudible: false,
  });

  test('one page: six encoders and LEVEL set the controls, three presses switch, FUNC + RATE is fine, G and H do nothing', async () => {
    const { fixture, plan, artifacts } = context;
    const runtime = fixture.runtime;
    const state = await inspectFxBoot(runtime, plan, ['digikit/prism'], artifacts, 'boot');
    context.rememberEpoch(state.epoch);
    const session = runtime.session;
    const debug = planSymbol(plan, 'phrase_ui_debug');
    const values = planSymbol(plan, 'phrase_ui_values');
    const binding = planSymbol(plan, 'phrase_ui_binding');
    const value = (index: number) => word(session, values + 4 * index);
    const allValues = () => Promise.all(Array.from({ length: PH.PH_PARAM_COUNT! }, (_, i) => value(i)));
    const page = () => prismPage(session, planSymbol(plan, 'phrase_ui_page'));
    const control = async () => {
      const words = await dspWords(session, planSymbol(plan, '_phrase_control_info'), 64);
      return (name: string) => words[PH[name]! / 4]!;
    };
    const status = async () => {
      const words = await dspWords(session, planSymbol(plan, '_phrase_status'), 32);
      return (name: string) => words[PH[name]! / 4]!;
    };
    const active = () => word(session, debug + 24);
    const turn = async (knob: string, ticks: number, ms = 120) => {
      await bounded(() => session.client.inputTurn(encoder(knob), ticks));
      await advance(session, ms);
    };

    await openPrismPage(session, debug);
    // the engine's first status and the resting header after the native passive view has drawn
    await advance(session, 2000);
    if (!(await active())) {
      await shot(session, artifacts, 'inactive-phrase');
      const diagnostic = await Promise.all(Array.from({ length: 8 }, (_, i) => word(session, binding + 4 * i)));
      expect(await active(), `the page is on top; binding=${JSON.stringify(diagnostic)}`).not.toBe(0);
    }
    const nativePage = await word(session, debug + 28);

    // The page at its defaults: the eight cells in their places, MIX on LEVEL, the zone and the engine's time in the header.
    const first = await page();
    const firstStatus = await status();
    record(artifacts, 'page-default', first);
    await shot(session, artifacts, 'default');
    expect(firstStatus('PH_S_MAGIC'), 'the engine publishes its status').toBe(PH.PH_STATUS_MAGIC);
    expect(firstStatus('PH_S_ERROR')).toBe(PH.PH_ERROR_NONE);
    expect(first.title).toBe('PRISM');
    expect(first.zone).toBe('ECHO');
    expect(first.time, 'the time the engine plays, in the header').toMatch(/^\d+ MS$/);
    expect(Math.abs(parseInt(first.time, 10) - firstStatus('PH_S_TIME_L') / 48)).toBeLessThanOrEqual(1);
    expect(first.mix).toBe(100);
    expect(first.cells.map((cell) => cell.label)).toEqual(['ZONE', 'RATE', 'RPTS', 'COLR', 'HALO', 'SKEW', 'INPT', 'XFAD']);
    expect(first.cells.map((cell) => cell.value)).toEqual(['ECHO', first.time, '48', '0', '0', '0', 'LIVE', '--']);
    expect(first.cells.map((cell) => cell.state)).toEqual(['', 'FREE', '', '', '', '', '', 'NO LOOP']);

    // The presses, at the default values. B: SYNC. The cell shows the note length, the provider sends its length in frames at the tempo, and
    // the engine takes it.
    await tap(session, 'PUSH B', 120);
    expect(await value(PH.PH_P_SYNC!)).toBe(1);
    let words = await control();
    expect(words('PH_C_FLAGS') & PH.PH_F_SYNC!).toBe(PH.PH_F_SYNC);
    const tempo = words('PH_C_TEMPO_120');
    expect(tempo, 'the tempo of the position suffix').toBeGreaterThanOrEqual(3600);
    expect(words('PH_C_SYNC_TIME'), 'a dotted eighth is 72 ticks').toBe(Math.floor((72 * 3_600_000) / tempo));
    expect((await page()).cells[1]).toMatchObject({ value: '1/8.', state: 'SYNC' });
    await advance(session, 1500);
    expect((await status())('PH_S_TIME_L')).toBe(words('PH_C_SYNC_TIME'));
    expect(Math.abs(parseInt((await page()).time, 10) - words('PH_C_SYNC_TIME') / 48)).toBeLessThanOrEqual(1);
    await shot(session, artifacts, 'sync');
    // With SYNC, RATE's encoder steps the note lengths, four ticks each, and the free rate is kept. (The stock input path does not deliver
    // an encoder's turns for about 0.3 s after its own press, finding 359: the wait above is past that.)
    const free = await value(PH.PH_P_RATE!);
    await turn('ENC B', 4);
    expect(await value(PH.PH_P_NOTE!)).toBe(9);
    expect(await value(PH.PH_P_RATE!), 'the free rate is kept').toBe(free);
    words = await control();
    expect(words('PH_C_SYNC_TIME'), 'a quarter is 96 ticks').toBe(Math.floor((96 * 3_600_000) / tempo));
    expect((await page()).cells[1]).toMatchObject({ value: '1/4', state: 'SYNC' });
    await tap(session, 'PUSH B', 120);
    expect(await value(PH.PH_P_SYNC!)).toBe(0);
    expect((await page()).cells[1]!.state).toBe('FREE');
    // A: FLIP.
    await tap(session, 'PUSH A', 120);
    expect(await value(PH.PH_P_FLIP!)).toBe(1);
    expect((await control())('PH_C_FLAGS') & PH.PH_F_FLIP!).toBe(PH.PH_F_FLIP);
    expect((await page()).cells[0]).toMatchObject({ value: 'ECHO', state: 'FLIP', lit: true });
    // C: HOLD. The engine reports it.
    await tap(session, 'PUSH C', 120);
    expect(await value(PH.PH_P_HOLD!)).toBe(1);
    expect((await control())('PH_C_FLAGS') & PH.PH_F_HOLD!).toBe(PH.PH_F_HOLD);
    expect((await page()).cells[2]).toMatchObject({ value: 'INF', state: 'HOLD', lit: true });
    await advance(session, 1500);
    expect((await status())('PH_S_STATE')).toBe(PH.PH_HELD);
    expect((await page()).cells[1]!.state, 'RATE is free again').toBe('FREE');
    await shot(session, artifacts, 'hold-flip');
    await tap(session, 'PUSH C', 120);
    await tap(session, 'PUSH A', 120);
    expect([await value(PH.PH_P_HOLD!), await value(PH.PH_P_FLIP!)]).toEqual([0, 0]);
    expect((await page()).cells[2]).toMatchObject({ value: '48', state: '', lit: false });
    await advance(session, 1500);

    // A, ZONE: four ticks a zone.
    await turn('ENC A', 4);
    expect(await value(PH.PH_P_ZONE!)).toBe(5);
    expect((await control())('PH_C_ZONE'), 'the control word counts zones from 1').toBe(6);
    expect((await page()).cells[0]!.value).toBe('LONG');
    expect((await page()).zone).toBe('LONG');
    await turn('ENC A', -4);
    expect(await value(PH.PH_P_ZONE!)).toBe(4);

    // B, RATE: a coarse step a tick; with FUNC held one tick is the fine step.
    const rate = await value(PH.PH_P_RATE!);
    await turn('ENC B', 1);
    expect(await value(PH.PH_P_RATE!)).toBe(rate + PH.PH_RATE_COARSE!);
    expect(float((await control())('PH_C_RATE'))).toBeCloseTo((rate + PH.PH_RATE_COARSE!) / PH.PH_RATE_MAX!, 5);
    await bounded(() => session.client.inputButton(button('FUNC'), true));
    try {
      await advance(session, 20);
      await turn('ENC B', 1);
      await turn('ENC B', 1);
    } finally {
      await bounded(() => session.client.inputButton(button('FUNC'), false));
      await advance(session, 80);
    }
    expect(await value(PH.PH_P_RATE!), 'FUNC + RATE: two fine steps').toBe(rate + PH.PH_RATE_COARSE! + 2);
    expect(await active(), 'FUNC with an encoder does not leave the page').not.toBe(0);
    // the engine glides to the longer time, and the RATE cell follows it
    await advance(session, 2500);
    const longer = await page();
    expect(parseInt(longer.cells[1]!.value, 10)).toBeGreaterThan(parseInt(first.time, 10));
    expect(longer.cells[1]!.value).toBe(longer.time);

    // C to F and LEVEL: one tick is one step.
    await turn('ENC C', 2);
    expect(await value(PH.PH_P_RPTS!)).toBe(50);
    expect(float((await control())('PH_C_RPTS'))).toBeCloseTo((50 * 0.75) / PH.PH_RPTS_UNITY!, 5);
    await turn('ENC D', -4);
    expect(await value(PH.PH_P_COLR!)).toBe(60);
    expect(float((await control())('PH_C_COLR'))).toBeCloseTo(-4 / 64, 5);
    await turn('ENC E', 5);
    expect(await value(PH.PH_P_HALO!)).toBe(5);
    expect(float((await control())('PH_C_HALO'))).toBeCloseTo(5 / 127, 5);
    await turn('ENC F', 3);
    expect(await value(PH.PH_P_SKEW!)).toBe(67);
    expect(float((await control())('PH_C_SKEW'))).toBeCloseTo(3 / 63, 5);
    await turn('LEVEL', -8);
    await turn('LEVEL', -2);
    expect(await value(PH.PH_P_MIX!)).toBe(90);
    expect(float((await control())('PH_C_MIX'))).toBeCloseTo(90 / 127, 5);
    const turned = await page();
    record(artifacts, 'page-turned', turned);
    expect(turned.cells.slice(2, 6).map((cell) => [cell.value, cell.state])).toEqual([
      ['50', ''],
      ['4', 'DARK'],
      ['5', ''],
      ['R 3', ''],
    ]);
    expect(turned.mix).toBe(90);

    // G and H, the presses of D to H and FUNC + NO: nothing changes, and the page stays.
    const before = await allValues();
    const cellsBefore = (await page()).cells.slice(6);
    const wordsBefore = await control();
    for (const knob of ['ENC G', 'ENC H']) {
      await turn(knob, 3);
      await turn(knob, -5);
    }
    for (const key of ['PUSH D', 'PUSH E', 'PUSH F', 'PUSH G', 'PUSH H']) await tap(session, key, 80);
    await tap(session, 'NO', 120, 'FUNC');
    expect(await active(), 'FUNC + NO is taken by the page').not.toBe(0);
    expect(await allValues()).toEqual(before);
    expect((await page()).cells.slice(6)).toEqual(cellsBefore);
    const wordsAfter = await control();
    for (const name of [
      'PH_C_ZONE',
      'PH_C_RATE',
      'PH_C_FLAGS',
      'PH_C_RPTS',
      'PH_C_COLR',
      'PH_C_HALO',
      'PH_C_SKEW',
      'PH_C_MIX',
      'PH_C_INPUT',
      'PH_C_XFADE',
      'PH_C_ARM_SEQ',
    ])
      expect(wordsAfter(name), name).toBe(wordsBefore(name));
    expect(wordsAfter('PH_C_INPUT'), 'the words kept for the loop are zero').toBe(0);

    // Another view over the page: the page owns nothing, and LEVEL there does not move MIX.
    const mix = await value(PH.PH_P_MIX!);
    await tap(session, 'SETTINGS', 100);
    expect(await active(), 'a covered FX page owns no controls').toBe(0);
    await turn('LEVEL', 1, 80);
    await tap(session, 'NO', 100);
    expect(await active(), 'back in the FX view the page is on top again').not.toBe(0);
    expect(await value(PH.PH_P_MIX!), 'LEVEL in another view does not change MIX').toBe(mix);
    // A press that is down while the view is covered: its release is lost, and the next press still switches.
    await bounded(() => session.client.inputButton(button('PUSH A'), true));
    await advance(session, 80);
    expect(await value(PH.PH_P_FLIP!)).toBe(1);
    await tap(session, 'SETTINGS', 100);
    await bounded(() => session.client.inputButton(button('PUSH A'), false));
    await advance(session, 80);
    await tap(session, 'NO', 100);
    await tap(session, 'PUSH A', 80);
    expect(await value(PH.PH_P_FLIP!), 'a release lost under another view does not hold the next press back').toBe(0);

    // UP, DOWN and FUNC + FX stay the FX view's: the next effect page is not PRISM's.
    await tap(session, 'FX', 120, 'FUNC');
    expect(await word(session, debug + 28), 'FUNC + FX leaves PRISM').not.toBe(nativePage);
    expect(await active()).toBe(0);
    await mainPage(session);
    await openPrismPage(session, debug);
    expect(await value(PH.PH_P_RPTS!), 'the values are the provider\'s: they are there again').toBe(50);
    // back to a plain state for the settings on the card
    await setPrism(session, values, PH.PH_P_MIX!, 'LEVEL', 100);
    await mainPage(session);
    await bounded(() => session.expectNoFault('PRISM native page gestures'));
  });
});
