import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';

describeOn('dt2', 'stock audio helper group', () => {
  const handle = withSession({ ...liveOptions('dt2', { fresh: true }), dsp: 'sharc', restart: false });
  test('admits an idle empty helper through POOL and keeps its group through PLAY', { timeout: 180_000 }, async () => {
    const { session } = handle;
    const image = await prepareRuntimePlanModulesOffline(
      session.options, session.runDir, session.runDir,
      ['DKRT_BOOT', 'tk_native_audio_active', 'pp_position'],
      { modules: ['core/track-keyboard', 'core/instrument-play', 'core/digikit-menu', 'core/health'] },
    );
    const word = (name: string, offset = 0) => session.client.memPeek(planSymbol(image.plan, name) + offset);
    const mask = async (name: string) => ((await word(`.sec..bss.${name}`)) ?? 0) >>> 16;
    await session.client.firmwareLoad({
      syx: image.syx, base_sha256: image.plan.base.syx_sha256, start: 'auto',
      force: true, auto_no: true, card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    await expectRuntimeUi(session, image.plan, session.runDir);
    const { edge, tap, turn } = acknowledgedPanel(session);
    await edge('TRK', true);
    try {
      await tap('TRIG 1');
    } finally {
      await edge('TRK', false);
    }
    const candidates = (await mask('eligible')) & ~1;
    expect(candidates).toBeGreaterThan(0);
    const helper = 31 - Math.clz32(candidates & -candidates);
    const helperMask = 1 << helper;
    await tap('SRC');
    for (let page = 0; page < 4; page++) await tap('PAGE TRIG');
    await turn(1, 12);
    await tap('PUSH H');
    for (let row = 1; row < helper; row++) await tap('DOWN');
    await tap('YES');
    await pollEmulated(session, () => mask('reserved'), (value) => value === helperMask, {
      emulatedMs: 4000, message: 'idle helper is admitted after its DSP quiet witness',
    });
    await tap('NO');
    await tap('KEYBOARD');
    const both = (value: number | null) => ((value ?? 0) & (helperMask | 1)) === (helperMask | 1);
    const chord = async () => {
      await edge('TRIG 9', true);
      await edge('TRIG 11', true);
      try {
        await pollEmulated(session, () => word('tk_native_audio_active'), both, {
          emulatedMs: 4000, message: 'same owner sends two pitches to distinct physical voices',
        });
      } finally {
        await edge('TRIG 11', false);
        await edge('TRIG 9', false);
      }
    };
    await chord();
    await tap('PLAY');
    await pollEmulated(session, () => word('pp_position', 20), (flags) => !!((flags ?? 0) & 2), {
      emulatedMs: 2000, message: 'native pattern is running',
    });
    expect((await mask('reserved')) & helperMask).toBe(helperMask);
    await chord();
    await tap('STOP');
  });
});
