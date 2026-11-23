import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { emulatedClockMs, pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';

const DISPLAY_BUDGET_MS = 150;

describeOn('dt2', 'track keyboard display', () => {
  const handle = withSession({ ...liveOptions('dt2', { fresh: true }), dsp: 'sharc', restart: false });
  test('shows playing and released notes without another UI action', { timeout: 180_000 }, async () => {
    const { session } = handle;
    const image = await prepareRuntimePlanModulesOffline(
      session.options, session.runDir, session.runDir,
      ['DKRT_BOOT', 'tk_native_audio_active'],
      { modules: ['core/track-keyboard', 'core/instrument-play', 'core/digikit-menu', 'core/health'] },
    );
    await session.client.firmwareLoad({
      syx: image.syx, base_sha256: image.plan.base.syx_sha256,
      start: 'auto', force: true, auto_no: true, card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    await expectRuntimeUi(session, image.plan, session.runDir);
    const { edge, tap, turn } = acknowledgedPanel(session);
    await tap('NO'); // Dismiss the stock filesystem acknowledgement.
    await edge('TRK', true);
    try {
      await tap('TRIG 1');
    } finally {
      await edge('TRK', false);
    }
    await tap('SRC');
    for (let i = 0; i < 4; i++) await tap('PAGE TRIG');
    await turn(1, 12); // KEYS A, POLY.
    await tap('KEYBOARD');

    const setWord = (addr: number, value: number) => {
      const data = Buffer.alloc(4);
      data.writeUInt32BE(value);
      return session.client.call('mem.cf.write', { addr, hex: data.toString('hex') });
    };
    await setWord(0x429368ca, 3); // Same stock USB/DIN receive setup as the MIDI ingress fixture.
    await setWord(0x42936872, 0); // Auto Channel 1.
    await setWord(0x429368ee, 1); // Receive Notes.
    await session.wait(500);

    const keys = async () => {
      const frame = await session.client.framebufferGet({ data: true });
      const pixels = Buffer.from(frame.data_b64 ?? '', 'base64');
      expect(pixels).toHaveLength(1024);
      const crop = Buffer.alloc(43 * 15);
      for (let x = 25; x <= 67; x++) {
        for (let y = 42; y <= 56; y++) {
          crop[(x - 25) * 15 + y - 42] = (pixels[x * 8 + 7 - (y >> 3)]! >> (y & 7)) & 1;
        }
      }
      return crop;
    };
    const idle = await keys();
    expect(idle.reduce((sum, pixel) => sum + pixel, 0)).toBeGreaterThan(250);
    const clock = async () => emulatedClockMs(await session.status());
    const activeMask = planSymbol(image.plan, 'tk_native_audio_active');
    const active = async () => ((await session.client.memPeek(activeMask)) ?? 0) & 1;
    const inputs = [
      { name: 'panel', send: (on: boolean) => session.client.inputButton(33, on) },
      { name: 'USB', send: (on: boolean) => session.client.call('midi.usb.in', { bytes: [on ? 0x90 : 0x80, 64, on ? 100 : 0] }) },
      { name: 'DIN', send: (on: boolean) => session.client.call('midi.in', { bytes: [on ? 0x90 : 0x80, 67, on ? 100 : 0] }) },
    ];
    const results = [];
    for (const { name, send } of inputs) {
      const measure = async (on: boolean) => {
        const before = await clock();
        await send(on);
        const last = await pollEmulated(session, async () => ({
          active: Boolean(await active()),
          highlighted: !(await keys()).equals(idle),
          ms: (await clock()) - before,
        }), (state) => (state.active === on && state.highlighted === on) || state.ms >= DISPLAY_BUDGET_MS, {
          emulatedMs: 1000, intervalMs: 2, message: name + ' KEYS display updates',
        });
        expect(last.active, name + ' native note reached its backend').toBe(on);
        return last.highlighted === on && last.ms <= DISPLAY_BUDGET_MS ? last.ms : null;
      };
      let on: number | null = null;
      try {
        on = await measure(true);
      } finally {
        results.push({ input: name, on, off: await measure(false) });
      }
    }
    console.info('KEYS note-to-display latency in emulated milliseconds', results);
    for (const { input, on, off } of results) {
      expect(on, input + ' note-on highlight').not.toBeNull();
      expect(off, input + ' note-off highlight').not.toBeNull();
    }
    await session.expectNoFault('KEYS note-driven display');
  });
});
