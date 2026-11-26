import { describeOn } from '@digi/regression/runtime/devices.ts';
import { liveOptions } from '@digi/regression/runtime/live-options.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { liveProtocol } from '@digi/regression/runtime/protocol.ts';
import { withSession } from '@digi/regression/support.ts';
import { freeClock, waitClock } from '@digi/test-support';
import { expect, test } from 'vitest';
import { trackKeyboardCard, trackKeyboardSymbols } from './track-card.ts';

// The settings of track 1, the first TkTrackConfig of the record (56 bytes on the ColdFire): POLY at +1, the chord at +8, the
// override at +55.
// The firmware's boot record writer alone (DKRT_SAVE_MODE bit 1, finding 40): what its power-off path stores. No serialization of
// the project runs, so only what is already in the working copy comes back.
const BOOT_RECORD_ONLY = 2;

describeOn('dt2', 'track keyboard settings in the working copy', () => {
  const handle = withSession({ ...trackKeyboardCard(liveOptions('dt2', { fresh: true })), dsp: 'sharc', restart: false });

  test('a KEYS edit comes back after the boot record alone is written, also when the pattern is left at once', { timeout: 600_000 }, async () => {
    const { session } = handle;
    const image = await prepareRuntimePlanModulesOffline(session.options, session.runDir, session.runDir, ['DKRT_BOOT', 'DKRT_SAVE_MODE', 'tk_work_info'], {
      modules: ['core/track-keyboard', 'core/health'],
    });
    let sym = (name: string) => planSymbol(image.plan, name);
    const word = async (addr: number) => ((await session.client.memPeek(addr)) ?? 0) >>> 0;
    const track1 = async () => {
      const editing = await word(sym('editing'));
      if (!editing) return { poly: -1, chord: -1, override: -1 };
      const at = async (offset: number) => (await word(editing + (offset & ~3))) >>> (8 * (3 - (offset & 3))) & 0xff;
      return { poly: await at(1), chord: await at(8), override: await at(55) };
    };
    const writes = () => word(sym('tk_work_info'));
    const boot = async () => {
      await expectRuntimeUi(session, image.plan, session.runDir, { emulatedMs: 180_000 });
      sym = await trackKeyboardSymbols(session, image.plan);
    };
    const restart = async () => {
      await session.client.call('mem.cf.poke', { addr: sym('DKRT_SAVE_MODE'), value: BOOT_RECORD_ONLY });
      const epoch = Number((await session.status()).epoch);
      const dk = liveProtocol(session);
      await dk.restart();
      await dk.waitForBoot();
      await expectRuntimeUi(session, image.plan, session.runDir, { afterEpoch: epoch, emulatedMs: 180_000 });
      sym = await trackKeyboardSymbols(session, image.plan);
    };
    await session.client.firmwareLoad({ syx: image.syx, base_sha256: image.plan.base.syx_sha256, start: 'auto', force: true, auto_no: true, card: session.cardPath });
    await session.client.call('run.mode', { mode: 'free' });
    await boot();
    const { edge, tap, turn } = acknowledgedPanel(session);
    await tap('NO'); // the stock FILE SYSTEM OK acknowledgement
    await edge('TRK', true);
    try {
      await tap('TRIG 1');
    } finally {
      await edge('TRK', false);
    }
    const keys = async () => {
      await tap('SRC');
      for (let page = 0; page < 4; page++) await tap('PAGE TRIG');
    };
    expect(await track1(), 'a fresh pattern keeps the defaults').toMatchObject({ poly: 0, override: 0 });

    // 1. POLY and MAJ on A01's track 1, no project save, then two quiet seconds.
    await keys();
    await turn(1, 12);
    await turn(2, 4);
    expect(await track1()).toEqual({ poly: 1, chord: 1, override: 1 });
    const before = await writes();
    await waitClock(freeClock(session), 3000);
    expect(await writes(), 'the edit reached the working copy after it settled').toBeGreaterThan(before);
    await restart();
    expect(await track1(), 'the boot brings the KEYS settings back without a save').toEqual({ poly: 1, chord: 1, override: 1 });

    // 2. CHRD to MIN, and A02 at once (well inside the two seconds): leaving the pattern writes it.
    await keys();
    await turn(2, 16); // MAJ (1) to MIN (5), four ticks a chord
    expect(await track1()).toMatchObject({ chord: 5 });
    await tap('PTN');
    await tap('TRIG 2');
    await waitClock(freeClock(session), 500);
    await restart();
    await tap('PTN');
    await tap('TRIG 1');
    await waitClock(freeClock(session), 500);
    expect(await track1(), 'the edit made just before leaving the pattern came back').toEqual({ poly: 1, chord: 5, override: 1 });
    expect(await word(sym('tk_work_info') + 4), 'every pattern had a record in the working copy').toBe(0);
    await session.expectNoFault('track keyboard settings in the working copy');
  });
});
