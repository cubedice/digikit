import { button, encoder } from '@digi/test-support';
import { expect, test } from 'vitest';
import { bounded, word } from '@digi/regression/runtime/fx-live.ts';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { inspectFxBoot, shot, withPreparedFxSession } from '@digi/regression/runtime/fx-session.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { advance, mainPage, openPrismPage, tap } from './ui-helpers.ts';

describeOn('dt2', 'DTII PRISM native performance pages', () => {
  const context = withPreparedFxSession('dt2', 'PRISM UI', {
    image: {
      name: 'prism-ui',
      modules: [
        'core/runtime',
        'core/dynreg',
        'core/dyndsp',
        'core/dynfx',
        'core/phrase-ui',
        'core/phrase-route',
        'core/digikit-menu',
        'core/health',
      ],
      inventory: ['digikit/prism'],
      symbols: ['DKRT_BOOT', 'fx.rows', 'phrase_ui_debug', 'phrase_ui_values', 'phrase_ui_binding'],
      policy: 'settings',
    },
    initial: 'phrase',
    presets: { phrase: { selection: ['modules/m0.dkm'] } },
    requireAudible: false,
  });

  test('one global entry contains five pages, remembers position and owns the input overlay', async () => {
    const { fixture, plan, artifacts } = context;
    const runtime = fixture.runtime;
    const state = await inspectFxBoot(runtime, plan, ['digikit/prism'], artifacts, 'boot');
    context.rememberEpoch(state.epoch);
    const session = runtime.session;
    const debug = planSymbol(plan, 'phrase_ui_debug');
    const values = planSymbol(plan, 'phrase_ui_values');
    const binding = planSymbol(plan, 'phrase_ui_binding');
    const page = () => word(session, debug + 4);
    const overlay = () => word(session, debug + 8);
    const value = (index: number) => word(session, values + 4 * index);
    const grid = async () =>
      Array.from(Buffer.from((await bounded(() => session.leds())).leds, 'hex').subarray(25, 41));
    await openPrismPage(session, debug);
    // Also qualify the resting header after the native passive view redraws.
    await advance(session, 2000);
    const active = await word(session, debug + 24);
    if (!active) {
      await shot(session, artifacts, 'inactive-phrase');
      const diagnostic = await Promise.all(Array.from({ length: 8 }, (_, i) => word(session, binding + 4 * i)));
      expect(active, `phrase owns the selected FX page; binding=${JSON.stringify(diagnostic)}`).not.toBe(0);
    }
    expect(await page(), 'a fresh owner shows DELAY').toBe(4);
    await tap(session, 'DOWN', 80);
    expect(await page(), 'LOOP follows DELAY').toBe(0);
    const nativePage = await word(session, debug + 28);
    const pictures: string[] = [];
    for (const [i, name] of ['loop', 'grain', 'move', 'space'].entries()) {
      if (i) await tap(session, 'DOWN', 80);
      expect(await page()).toBe(i);
      expect(await word(session, debug + 28), 'internal pages share one global page code').toBe(nativePage);
      pictures.push(await shot(session, artifacts, name));
    }
    expect(new Set(pictures).size).toBe(4);
    await bounded(() => session.client.inputButton(button('FUNC'), true));
    try {
      await advance(session, 5);
      await bounded(() => session.client.inputButton(button('FX'), true));
      await advance(session, 700);
      const valuesFrame = await shot(session, artifacts, 'space-values');
      expect(valuesFrame, 'native held-FX values mode reveals formatted values').not.toBe(pictures[3]);
      expect(await word(session, debug + 28)).toBe(nativePage);
    } finally {
      await bounded(() => session.client.inputButton(button('FX'), false));
      await advance(session, 80);
      await bounded(() => session.client.inputButton(button('FUNC'), false));
      await advance(session, 5);
    }
    const releasedActive = await word(session, debug + 24);
    const releasedPage = await word(session, debug + 28);
    if (!releasedActive || releasedPage !== nativePage) {
      await shot(session, artifacts, 'after-values-release');
      expect({ active: releasedActive, page: releasedPage }, 'held-FX release retains the active phrase page').toEqual({
        active: 1,
        page: nativePage,
      });
    }
    await tap(session, 'DOWN', 80);
    const wrappedPage = await page();
    if (wrappedPage !== 4) {
      await shot(session, artifacts, 'after-values-navigation');
      expect(wrappedPage, 'local DOWN after held-FX goes on to DELAY').toBe(4);
    }
    await tap(session, 'UP', 80);
    expect(await page()).toBe(3);
    await tap(session, 'FX', 80, 'FUNC');
    expect(await word(session, debug + 28), 'global advance leaves PRISM').not.toBe(nativePage);
    await mainPage(session);
    await openPrismPage(session, debug);
    expect(await page(), 'return recalls SPACE').toBe(3);

    const nativeGrid = await grid();
    await tap(session, 'PUSH H', 80);
    expect(await overlay()).toBe(1);
    await shot(session, artifacts, 'inputs');
    const initial = await value(21);
    expect(await grid(), 'input mode owns only the next-source grid').toEqual(
      Array.from({ length: 16 }, (_, i) => (initial & (1 << i) ? 0x71 : 0x02)),
    );
    await tap(session, 'TRIG 3', 80);
    const selectedAfterTap = await value(21);
    if (selectedAfterTap !== (initial ^ 4)) {
      const bound = await Promise.all(Array.from({ length: 8 }, (_, i) => word(session, binding + 4 * i)));
      await shot(session, artifacts, 'input-selection-failed');
      expect(selectedAfterTap, `TRIG 3 selection; binding=${JSON.stringify(bound)}`).toBe(initial ^ 4);
    }
    expect((await grid())[2]).toBe(initial & 4 ? 0x02 : 0x71);
    const gain = await value(24);
    await bounded(() => session.client.inputButton(button('TRIG 3'), true));
    try {
      await advance(session, 20);
      await bounded(() => session.client.inputTurn(encoder('LEVEL'), -8));
      await advance(session, 80);
      expect(await value(24), 'held TRIG edits capture gain').toBeLessThan(gain);
    } finally {
      await bounded(() => session.client.inputButton(button('TRIG 3'), false));
      await advance(session, 80);
    }
    expect(await value(21), 'gain edit does not toggle selection').toBe(initial ^ 4);
    await tap(session, 'UP', 80);
    expect(await overlay()).toBe(1);
    expect(await page()).toBe(3);
    await tap(session, 'NO', 80);
    expect(await overlay()).toBe(0);
    expect(await grid(), 'closing IN restores the native TRIG grid').toEqual(nativeGrid);
    expect(await word(session, debug + 24), 'NO closes just the overlay').not.toBe(0);

    const fade = await value(20);
    await tap(session, 'PUSH H', 80);
    await tap(session, 'SETTINGS', 100);
    expect(await overlay(), 'another view closes the input overlay').toBe(0);
    expect(await word(session, debug + 24), 'a covered FX page owns no controls').toBe(0);
    await bounded(() => session.client.inputTurn(encoder('LEVEL'), 1));
    await advance(session, 80);
    await tap(session, 'NO', 100);
    expect(await word(session, debug + 24), 'returning to FX restores only its page').not.toBe(0);
    expect(await overlay()).toBe(0);
    expect(await value(20), 'LEVEL in another view cannot change FADE').toBe(fade);

    // Cover the view while H is still owned; its release may reach only the
    // new modal. The next initial H press must still open IN normally.
    await bounded(() => session.client.inputButton(button('PUSH H'), true));
    await advance(session, 80);
    await tap(session, 'SETTINGS', 100);
    await bounded(() => session.client.inputButton(button('PUSH H'), false));
    await advance(session, 80);
    await tap(session, 'NO', 100);
    await tap(session, 'PUSH H', 80);
    expect(await overlay(), 'a missed covered H release cannot suppress its next press').toBe(1);

    const selected = await value(21);
    await bounded(() => session.client.inputButton(button('TRIG 4'), true));
    await advance(session, 80);
    await tap(session, 'SETTINGS', 100);
    await bounded(() => session.client.inputButton(button('TRIG 4'), false));
    await advance(session, 80);
    await tap(session, 'NO', 100);
    expect(await value(21), 'losing the view cancels the held source tap').toBe(selected);
    await tap(session, 'UP', 80);
    expect(await page(), 'a covered TRIG release cannot leave navigation modified').toBe(2);
    await tap(session, 'PUSH H', 80);
    await tap(session, 'TRIG 4', 80);
    expect(await value(21), 'the next source tap works after the covered release').toBe(selected ^ 8);
    await tap(session, 'NO', 80, 'FUNC');
    expect(await overlay(), 'CLEAR keeps IN open').toBe(1);
    expect(await value(20)).toBe(0);
    await tap(session, 'FX', 80, 'FUNC');
    expect(await overlay()).toBe(0);
    expect(await word(session, debug + 28)).not.toBe(nativePage);
    await mainPage(session);
    await bounded(() => session.expectNoFault('PRISM native UI gestures'));
  });
});
