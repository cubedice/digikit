// device-literals: allow (pins to the native pattern clock and track records)
import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { trackType } from '@digi/regression/runtime/project-kit.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';

const TRACK = 9;
const QUARTER_TICKS = 21_600_000;
const SLOT_TICKS = QUARTER_TICKS / 8;
const NATIVE_PHASE = 0x80004880;
const NATIVE_TEMPO = 0x402b67fc;
const NATIVE_PATTERN = 0x4316501c;
const NATIVE_TRACK_BYTES = 0x6a5;
const NATIVE_DEFAULT_LENGTH = 0x481;

describeOn('dt2', 'track 10 arpeggio phase', () => {
  const handle = withSession({ ...liveOptions('dt2', { fresh: true }), dsp: 'sharc', restart: false });
  test('keeps short-gate generated notes on the running native sequence grid', { timeout: 240_000 }, async () => {
    const { session } = handle;
    const { plan, syx } = await prepareRuntimePlanModulesOffline(
      session.options,
      session.runDir,
      session.runDir,
      ['DKRT_BOOT', 'pp_position', 'tk_native_try_seed_send', 'tk_sequence_audio_hits'],
      { modules: ['core/track-keyboard', 'core/instrument-play', 'core/digikit-menu', 'core/health'] },
    );
    await session.client.firmwareLoad({
      syx,
      base_sha256: plan.base.syx_sha256,
      start: 'auto',
      force: true,
      auto_no: true,
      card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    await expectRuntimeUi(session, plan, session.runDir);

    const { edge, tap, turn } = acknowledgedPanel(session);
    await tap('NO'); // Acknowledge the stock filesystem modal.
    await edge('TRK', true);
    try {
      await tap('TRIG 10');
    } finally {
      await edge('TRK', false);
    }
    expect(await trackType(session, TRACK + 1), 'track 10 uses a stock audio machine').toBeLessThan(5);
    await tap('SRC');
    await tap('RECORD');
    await tap('TRIG 1');
    await tap('RECORD');
    for (let page = 0; page < 3; ++page) await tap('PAGE TRIG');
    await turn(1, 12); // ARP on.
    await turn(3, -30);
    await turn(3, -30); // GATE 1%, roughly one 32-frame audio block at this tempo.
    await turn(4, 12); // RATE from 1/16 to 1/32.

    const word = async (addr: number) => {
      const value = await session.client.memPeek(addr);
      if (value === null) throw new Error(`Unreadable native timing word 0x${addr.toString(16)}`);
      return value >>> 0;
    };
    const pattern = await word(NATIVE_PATTERN);
    expect(pattern, 'a stock pattern is selected').toBeGreaterThan(0);
    // A long native LEN gives this programmed trig a full cycle without any second seed.
    const length = pattern + TRACK * NATIVE_TRACK_BYTES + NATIVE_DEFAULT_LENGTH;
    await session.client.call('mem.cf.write', { addr: length, hex: '5e' });
    expect(((await word(length & ~3)) >>> (8 * (3 - (length & 3)))) & 255).toBe(94);
    const tempo = await word(NATIVE_TEMPO);
    expect(tempo, 'the native tempo is valid').toBeGreaterThanOrEqual(3600);
    expect(tempo).toBeLessThanOrEqual(36000);
    const ticksPerMs = tempo * 3;

    const position = planSymbol(plan, 'pp_position');
    const hits = planSymbol(plan, 'tk_sequence_audio_hits');
    const readPosition = (index: number) => word(position + 4 * index);
    const sequenceBefore = await word(hits);
    const boundariesBefore = await readPosition(3);
    await session.client.pcProbeSet({
      keep: 128,
      probes: [
        {
          pc: planSymbol(plan, 'tk_native_try_seed_send'),
          label: 'arp-native-send',
          words: [NATIVE_PHASE, position + 12, position + 20, position + 28, position + 44],
          stack: 4,
        },
      ],
    });
    const reached = async () => ({ sequence: await word(hits), boundaries: await readPosition(3) });
    await tap('PLAY');
    try {
      await pollEmulated(session, reached, (value) => value.sequence > sequenceBefore && value.boundaries >= boundariesBefore + 17, {
        emulatedMs: 6000,
        intervalMs: 20,
        message: 'a programmed track 10 seed played across a full native pattern cycle',
      });
      const [report] = (await session.client.pcProbeReport()).probes;
      const readProbeWord = (entry: string | null | undefined) => {
        if (entry === null || entry === undefined) throw new Error('Incomplete native timing probe');
        return Number(entry);
      };
      const sends = (report?.hits ?? [])
        .filter((hit) => readProbeWord(hit.stack[1]) === TRACK)
        .map((hit) => {
          const [phase, boundary, flags, step, deadline] = hit.words.map(readProbeWord) as [
            number,
            number,
            number,
            number,
            number,
          ];
          const sinceBoundary = (phase - deadline) | 0;
          const slot = Math.round(sinceBoundary / SLOT_TICKS);
          return {
            boundary,
            flags,
            step,
            phase,
            slot,
            velocity: readProbeWord(hit.stack[3]),
            residual: sinceBoundary - slot * SLOT_TICKS,
          };
        })
        .filter((hit) => (hit.flags & 3) === 3 && hit.boundary < boundariesBefore + 17);
      const onsets = sends.filter((hit) => hit.velocity > 0);
      const gateOffs = sends.filter((hit) => hit.velocity === 0);
      const offsetsMs = onsets.map((hit) => hit.residual / ticksPerMs);
      const nativeSlots = onsets.map((hit) => [hit.step, hit.slot]);
      console.info('Track 10 1/32 native phase', {
        tempo: tempo / 120,
        count: onsets.length,
        gateOffs: gateOffs.length,
        offsetsMs: offsetsMs.map((offset) => Math.round(offset * 100) / 100),
        spreadMs: Math.max(...offsetsMs) - Math.min(...offsetsMs),
        nativeSlots,
      });
      // A late send can still be heard within its own subdivision. Check that
      // scheduler delays never cause missing, duplicated, or reordered slots.
      expect(nativeSlots, 'one native note-on per 1/32 subdivision in order').toEqual(
        Array.from({ length: 32 }, (_, slot) => [slot >> 1, slot & 1]),
      );
      expect(gateOffs.length, 'generated notes reached the native audio note-off path').toBe(onsets.length);
    } finally {
      await session.client.pcProbeSet({ probes: [] });
      await tap('STOP');
    }
  });
});
