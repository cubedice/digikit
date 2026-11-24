import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { REDRAW } from '@digi/regression/runtime/fx-live.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { sessionRt } from '@digi/regression/runtime/session-rt.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';
import { trackKeyboardCard, trackKeyboardSymbols } from './track-card.ts';

describeOn('dt2', 'shared keyboard pattern change', () => {
  const handle = withSession({ ...trackKeyboardCard(liveOptions('dt2', { fresh: true })), dsp: 'sharc', restart: false });
  test('intercepts the first trig of queued and direct-start patterns', { timeout: 300_000 }, async () => {
    const { session } = handle;
    const image = await prepareRuntimePlanModulesOffline(
      session.options,
      session.runDir,
      session.runDir,
      ['DKRT_BOOT', 'pp_position', 'pattern_slots'],
      { modules: ['core/track-keyboard', 'core/digikit-menu', 'core/health'] },
    );
    await session.client.firmwareLoad({
      syx: image.syx,
      base_sha256: image.plan.base.syx_sha256,
      start: 'auto',
      force: true,
      auto_no: true,
      card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    await expectRuntimeUi(session, image.plan, session.runDir);
    const sym = await trackKeyboardSymbols(session, image.plan);
    const { edge, tap, turn } = acknowledgedPanel(session);
    const modified = async (modifier: string, key: string) => {
      await edge(modifier, true);
      try {
        await tap(key);
      } finally {
        await edge(modifier, false);
      }
    };
    const first = 0x41776cb8;
    const second = first + 0x1db8c;
    const untilSelected = async (address: number) =>
      pollEmulated(session, () => session.client.memPeek(0x4316501c), (n) => n === address, {
        emulatedMs: 5000,
        message: 'stock selected pattern changed',
      });
    const position = sym('pp_position');
    const read = (word: number) => session.client.memPeek(position + 4 * word);
    const hits = () => session.client.memPeek(sym('tk_sequence_audio_hits'));
    const { ui } = sessionRt(session);
    const selectPattern = (step: number) =>
      ui.tap(`TRIG ${step}`, { held: 'PTN', until: { ms: 80, why: REDRAW } });
    const scheduledPattern = async () => {
      const status = await session.status();
      if (status.cf.error) throw new Error(`Pattern switch fault: ${status.cf.error}`);
      return session.client.memPeek(0x4316501c);
    };

    await modified('TRK', 'TRIG 1');
    await modified('PTN', 'TRIG 2');
    await untilSelected(second);
    await tap('SRC');
    const firstTrig = async () => Boolean(((await session.client.memPeek(second)) ?? 0) & 0x10000);
    await tap('RECORD');
    if (await firstTrig()) {
      await tap('TRIG 1');
      await pollEmulated(session, firstTrig, (set) => !set, {
        emulatedMs: 3000,
        message: 'old fixture trig cleared before recording a provably unconditional note',
      });
    }
    await tap('TRIG 1');
    await pollEmulated(session, firstTrig, Boolean, {
      emulatedMs: 3000,
      message: 'destination step 1 contains a native trig',
    });
    expect(((await session.client.memPeek(second)) ?? 0) & 1, 'destination step 2 has no trig').toBe(0);
    await tap('RECORD');
    await tap('SRC');
    await tap('PAGE TRIG');
    await tap('PAGE TRIG');
    await tap('PAGE TRIG');
    await turn(1, 12); // ARP on A02 only.
    await turn(3, 30);
    const pendingPattern = await session.client.memPeek(sym('pattern_slots') + 4);
    expect(pendingPattern, 'shared settings for A02').toBeGreaterThan(0);
    expect((await session.client.memPeek(pendingPattern!))! >>> 24, 'A02 ARP enabled').toBe(1);
    // Set different native lengths in the disposable fixture. The cold
    // snapshot has two 128-step patterns, which would each take 16 seconds.
    await session.client.call('mem.cf.write', { addr: first + 0x1db74, hex: '0010' });
    await session.client.call('mem.cf.write', { addr: second + 0x1db74, hex: '0008' });

    const switched = async (expectedHits: number, request: () => Promise<unknown>) => {
      await modified('PTN', 'TRIG 1');
      await untilSelected(first);
      await tap('PLAY');
      try {
        await pollEmulated(session, () => read(6), (n) => n === first, {
          emulatedMs: 5000,
          message: 'pattern A01 is playing',
        });
        await ui.wait({ ms: 350, why: 'A01 advances beyond the transport start' });
        expect(await hits()).toBe(expectedHits - 1);
        await request();
        await pollEmulated(session, scheduledPattern, (n) => n === second, {
          emulatedMs: 8_000,
          intervalMs: 35,
          message: 'the native sequencer schedules the destination pattern',
        });
        await pollEmulated(session, () => read(6), (n) => n === second, {
          emulatedMs: 500,
          intervalMs: 10,
          message: 'the scheduled destination pattern is audible',
        });
        await pollEmulated(session, hits, (n) => n === expectedHits, {
          emulatedMs: 150,
          intervalMs: 5,
          message: 'A02 step 1 was resolved by its shared arpeggiator',
        });
        expect(await read(7)).toBeLessThan(2);
        return await session.client.memPeek(0x43165028);
      } finally {
        if ((await session.status()).mode === 'free') await tap('STOP');
      }
    };
    const destinationKit = await switched(1, () => selectPattern(2));
    expect(destinationKit).toBeGreaterThan(0);
    // Exercise the stock sequencer's direct-start request from finding 365.
    // Supplying the pattern and kit tests the native countdown and boundary.
    const request = Buffer.alloc(20);
    request.writeUInt32BE(second, 0);
    request.writeUInt32BE(destinationKit!, 4);
    request.writeUInt32BE(1, 8); // Start from step 1.
    request.writeUInt32BE(1, 16); // Pending direct request.
    await switched(2, () => session.client.call('mem.cf.write', {
      addr: 0x431650d0,
      hex: request.toString('hex'),
    }));
    await session.expectNoFault('shared keyboard pattern change');
  });
});
