import path from 'node:path';
import { describeOn } from '@digi/regression/runtime/devices.ts';
import { dspWords } from '@digi/regression/runtime/dsp-state.ts';
import { rpc } from '@digi/regression/runtime/guest-access.ts';
import { liveOptions } from '@digi/regression/runtime/live-options.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { readFloatWav } from '@digi/regression/runtime/pcm-files.ts';
import { waitEmulated } from '@digi/regression/runtime/port-compat.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';
import { trackKeyboardCard, trackKeyboardSymbols } from './track-card.ts';

// Stock chords (core/dsp-voice `_cs_voice`, docs/findings/375-stock-voice-chords.md): with KEYS POLY and a chord, track 1's stock ONESHOT voice plays
// the whole chord by itself. The track keyboard sends the chord's first note and the chord's shape; the DSP renders the other notes as copies of the
// voice. The guard ends a copy whose block is not finite. Further keys of CHRD PLAY play on hidden voices, at their own velocity.

const AUDIO_RATE = 48_000;
const WINDOW = 4096;
const NAN = 0x7fc00000;

/** The level of one frequency in a settled window of one channel. */
function tone(pcm: Float32Array, frequency: number, channel: 0 | 1): number {
  expect(pcm.length).toBeGreaterThanOrEqual(2 * WINDOW);
  const offset = Math.floor((pcm.length / 2 - WINDOW) / 2);
  let real = 0;
  let imaginary = 0;
  for (let i = 0; i < WINDOW; i++) {
    const window = (1 - Math.cos((2 * Math.PI * i) / (WINDOW - 1))) / 2;
    const value = pcm[2 * (i + offset) + channel]! * window;
    const phase = (2 * Math.PI * frequency * i) / AUDIO_RATE;
    real += value * Math.cos(phase);
    imaginary += value * Math.sin(phase);
  }
  return Math.hypot(real, imaginary) / WINDOW;
}

function fundamental(pcm: Float32Array, highest = 550): number {
  // Track 1 of the stock test card is a one-second 220 Hz sine.
  let frequency = 0;
  let maximum = 0;
  for (let probe = 100; probe <= highest; probe += 1) {
    const level = tone(pcm, probe, 0);
    if (level > maximum) {
      maximum = level;
      frequency = probe;
    }
  }
  expect(maximum, 'the stock sample is audible').toBeGreaterThan(0.001);
  return frequency;
}

const hex8 = (value: number) => (value >>> 0).toString(16).padStart(8, '0');
const asFloat = (word: number) => new Float32Array(new Uint32Array([word]).buffer)[0]!;

describeOn('dt2', 'stock chords on one track', () => {
  const handle = withSession({ ...trackKeyboardCard(liveOptions('dt2', { fresh: true })), dsp: 'sharc', restart: false });
  test("KEYS POLY with a chord plays it on the track's own stock voice", { timeout: 300_000 }, async () => {
    const { session } = handle;
    const image = await prepareRuntimePlanModulesOffline(
      session.options, session.runDir, session.runDir,
      ['DKRT_BOOT'],
      { modules: ['core/track-keyboard', 'core/health', 'core/dsp-voice'] },
    );
    const dsp = (name: string) => planSymbol(image.plan, name);
    await session.client.firmwareLoad({
      syx: image.syx, base_sha256: image.plan.base.syx_sha256, start: 'auto',
      force: true, auto_no: true, card: session.cardPath,
    });
    await session.client.call('run.mode', { mode: 'free' });
    await expectRuntimeUi(session, image.plan, session.runDir);
    const sym = await trackKeyboardSymbols(session, image.plan);
    const word = async (name: string) => ((await session.client.memPeek(sym(name))) ?? 0) >>> 0;
    const { edge, tap, turn } = acknowledgedPanel(session);
    await tap('NO'); // the stock FILE SYSTEM OK acknowledgement
    await edge('TRK', true);
    try {
      await tap('TRIG 1');
    } finally {
      await edge('TRK', false);
    }
    await tap('SRC');
    for (let page = 0; page < 4; page++) await tap('PAGE TRIG');
    await turn(1, 12); // KEYS POLY on
    await tap('KEYBOARD');

    const render = async (label: string, during?: () => Promise<void>, key = 'TRIG 9') => {
      await waitEmulated(session, 1300); // the sample lasts one second at its own pitch
      const file = path.join(session.runDir, `chord-${label}.wav`);
      await edge(key, true);
      try {
        await waitEmulated(session, 50);
        await during?.();
        await session.client.call('pcm.tap', { action: 'capture', path: file });
        try {
          await waitEmulated(session, 200);
        } finally {
          await session.client.call('pcm.tap', { action: 'capture_stop' });
        }
      } finally {
        await edge(key, false);
      }
      return readFloatWav(file);
    };
    const THIRD = 2 ** (4 / 12);
    const FIFTH = 2 ** (7 / 12);
    const SECOND = 2 ** (2 / 12); // not in the chord
    const single = await render('single');
    const f0 = fundamental(single);
    const levels = (pcm: Float32Array) =>
      Object.fromEntries(
        ([['root', 1], ['second', SECOND], ['third', THIRD], ['fifth', FIFTH]] as const).map(([name, ratio]) => [
          name,
          [tone(pcm, f0 * ratio, 0), tone(pcm, f0 * ratio, 1)],
        ]),
      ) as Record<'root' | 'second' | 'third' | 'fifth', [number, number]>;
    const reference = levels(single);

    await turn(2, 4); // KEYS CHRD MAJ
    let held = { active: 0, vmask: 0, copies: 0, shape: 0, faults: 0 };
    const triad = levels(
      await render('triad', async () => {
        const row = await dspWords(session, dsp('_cs_cfg'), 6);
        held = {
          active: await word('tk_native_audio_active'),
          vmask: (await dspWords(session, dsp('_cs_vmask'), 1))[0]!,
          copies: row[0]!,
          shape: row[5]!,
          faults: (await dspWords(session, dsp('_cs_faults'), 1))[0]!,
        };
      }),
    );

    // The guard: NaN in the decimator state of the first copy (the third) of both voices of the pair while the chord is held. The copies of
    // voice v are 0x588 bytes apart.
    let guard = { faults: 0, active: 0 };
    const guarded = await render('guard', async () => {
      const copies = [dsp('_cs_rec'), dsp('_cs_rec') + 0x588];
      await session.client.call('run.mode', { mode: 'paused' });
      try {
        const lines: string[] = [];
        for (const copy of copies) {
          const old = await dspWords(session, copy + 0x104, 8);
          old.forEach((w, i) => lines.push(`DM ${(copy + 0x104 + 4 * i).toString(16)} ${hex8(NAN)} was ${hex8(w)}`));
        }
        await rpc(session, 'dsp.patch', { text: `BASE ${'0'.repeat(64)}\n${lines.join('\n')}\n` });
      } finally {
        await session.client.call('run.mode', { mode: 'free' });
      }
      await waitEmulated(session, 60);
      let active = 0;
      for (const copy of copies) active |= (await dspWords(session, copy + 0x1b8, 1))[0]! & 0xff;
      guard = { faults: (await dspWords(session, dsp('_cs_faults'), 1))[0]!, active };
    });
    const guardedLevels = levels(guarded);
    const again = levels(await render('again'));

    // Hidden voices: CHRD PLAY again, two keys on track 1 with no helper. The second key plays on a hidden voice; the first key up fades its own
    // note while the second plays on, and its stock note-off waits for the second.
    await turn(2, -4); // KEYS CHRD PLAY
    const single13 = await render('single-13', undefined, 'TRIG 13');
    const f1 = fundamental(single13);
    await waitEmulated(session, 1300);
    let both = { active: 0, hidden: 0 };
    const capture = async (label: string) => {
      const file = path.join(session.runDir, `hidden-${label}.wav`);
      await session.client.call('pcm.tap', { action: 'capture', path: file });
      try {
        await waitEmulated(session, 120);
      } finally {
        await session.client.call('pcm.tap', { action: 'capture_stop' });
      }
      return readFloatWav(file);
    };
    let pair: Float32Array;
    let after9: Float32Array;
    await edge('TRIG 9', true);
    try {
      await waitEmulated(session, 40);
      await edge('TRIG 13', true);
      try {
        await waitEmulated(session, 60);
        const hidden = await dspWords(session, dsp('_hv_state') + 32, 1);
        both = { active: await word('tk_native_audio_active'), hidden: hidden[0]! };
        pair = await capture('pair');
        await edge('TRIG 9', false);
        await waitEmulated(session, 150);
        after9 = await capture('after-9');
      } finally {
        await edge('TRIG 13', false);
      }
    } finally {
      await edge('TRIG 9', false);
    }
    // The hidden key up: its gate closes at its own velocity, so its fade keeps its level (factor 1 for two keys of one velocity).
    await waitEmulated(session, 20);
    const released = await dspWords(session, dsp('_hv_state') + 32, 8);
    await waitEmulated(session, 280);
    const tail = await capture('tail');
    const at = (pcm: Float32Array) => ({ f0: tone(pcm, f0, 0), f1: tone(pcm, f1, 0), finite: pcm.every(Number.isFinite) });
    const releasedWord = released[0]!;
    const hiddenReport = {
      f1, both, pair: at(pair!), after9: at(after9!), tail: at(tail), faults: (await dspWords(session, dsp('_cs_faults'), 1))[0]!,
      released: { gate: (releasedWord >>> 17) & 1, velocity: releasedWord & 127, factor: asFloat(released[7]!) },
    };
    console.info('HIDDEN VOICES', JSON.stringify(hiddenReport));
    await waitEmulated(session, 1300); // the first key's own sample (one second) ends: nothing of the track sounds
    const hmaskIdle = (await dspWords(session, dsp('_cs_hmask'), 1))[0]!;
    const vmaskIdle = (await dspWords(session, dsp('_cs_vmask'), 1))[0]!;
    // POLY off: the firmware plays the key itself (a stock note-on the track keyboard does not see). After the hidden voices it plays at its level.
    await turn(1, -12);
    const stockAfterHidden = levels(await render('stock-after-hidden'));
    await turn(1, 12);

    // The budget: with a limit no block can meet, a chord's copies are not rendered: they end and are counted, the owner plays. With the limit back,
    // the next chord is whole.
    const pokeWord = async (address: number, value: number) => {
      await session.client.call('run.mode', { mode: 'paused' });
      try {
        const old = (await dspWords(session, address, 1))[0]!;
        await rpc(session, 'dsp.patch', { text: `BASE ${'0'.repeat(64)}\nDM ${address.toString(16)} ${hex8(value)} was ${hex8(old)}\n` });
      } finally {
        await session.client.call('run.mode', { mode: 'free' });
      }
    };
    const limit = (await dspWords(session, dsp('_cs_limit'), 1))[0]!;
    const skipsBefore = (await dspWords(session, dsp('_cs_skips'), 1))[0]!;
    await turn(2, 4); // KEYS CHRD MAJ
    await pokeWord(dsp('_cs_limit'), 1);
    const starved = levels(await render('starved'));
    const skips = (await dspWords(session, dsp('_cs_skips'), 1))[0]! - skipsBefore;
    await pokeWord(dsp('_cs_limit'), limit);
    const restored = levels(await render('restored'));
    // After the chord's note-off a stock note-on on the track is one note: the chord's shape went with it.
    await turn(1, -12);
    const stockAfterChord = levels(await render('stock-after-chord'));
    await turn(1, 12);
    console.info('BUDGET', JSON.stringify({ limit, hmaskIdle, vmaskIdle, skips, starved, restored, stockAfterHidden, stockAfterChord }));

    // Velocity: MIDI notes on track 1 (USB, Auto Channel 1), CHRD PLAY. The second key plays on hidden voice 1 at the level the stock voice gives its
    // velocity: its factor (`_hv_state` word 7) is the track's velocity curve at its velocity over the curve at the owner's (word 4 holds both
    // velocities and the curve), and its level follows the factor.
    await turn(2, -4); // KEYS CHRD PLAY
    const setWord = (addr: number, value: number) => {
      const data = Buffer.alloc(4);
      data.writeUInt32BE(value);
      return session.client.call('mem.cf.write', { addr, hex: data.toString('hex') });
    };
    await setWord(0x429368ca, 3); // the stock USB/DIN receive setup of the MIDI ingress test
    await setWord(0x42936872, 0); // Auto Channel 1
    await setWord(0x429368ee, 1); // Receive Notes
    const midi = (bytes: number[]) => session.client.call('midi.usb.in', { bytes });
    await waitEmulated(session, 1300);
    await midi([0x90, 67, 100]);
    await waitEmulated(session, 50);
    const alone67 = await capture('velocity-67');
    await midi([0x80, 67, 0]);
    const f67 = fundamental(alone67, 1200);
    const velocityPair = async (label: string, owner: number, second: number) => {
      await waitEmulated(session, 1300);
      await midi([0x90, 60, owner]);
      await waitEmulated(session, 40);
      await midi([0x90, 67, second]);
      await waitEmulated(session, 60);
      const states = await dspWords(session, dsp('_hv_state'), 32); // track 1: the owner and hidden voices 1 .. 3, 8 words each
      const taken = [1, 2, 3].find((h) => {
        const word = states[8 * h]!;
        return word >>> 24 === 0xc6 && ((word >>> 17) & 1) === 1 && ((word >>> 7) & 127) === 67;
      });
      expect(taken, 'the second key plays on a hidden voice').toBeDefined();
      const state = states.slice(8 * taken!, 8 * taken! + 8);
      const pcm = await capture(`velocity-${label}`);
      await midi([0x80, 67, 0, 0x80, 60, 0]);
      const key = state[4]!;
      return {
        owner: key & 0x7f,
        curve: (key >>> 7) & 7,
        velocity: (key >>> 18) & 0x7f,
        factor: asFloat(state[7]!),
        level: tone(pcm, f67, 0),
        finite: pcm.every(Number.isFinite),
      };
    };
    const even = await velocityPair('even', 100, 100);
    const soft = await velocityPair('soft', 100, 40);
    const loud = await velocityPair('loud', 60, 100); // (curve 4 needs a factor of 4.3: below the cap of 8)
    console.info('VELOCITY', JSON.stringify({ f67, even, soft, loud }));
    console.info('STOCK CHORD', JSON.stringify({ f0, reference, triad, held, guard, guarded: guardedLevels, again }));

    const root = reference.root[0];
    expect(held.active, 'the chord plays on track 1 alone: no helper').toBe(1);
    expect(held.vmask & 3, 'voices 0 and 1 take the chord path').toBe(3);
    expect(held.copies, 'two copies: the third and the fifth').toBe(2);
    expect(held.shape >>> 24, 'the shape word came from the frame').toBe(0xc5);
    expect(held.faults).toBe(0);
    expect(guard.faults, 'the guard counted both copies').toBeGreaterThanOrEqual(2);
    expect(guard.active, 'both copies ended').toBe(0);
    expect(guarded.every(Number.isFinite), 'no sample of the track went bad').toBe(true);
    for (const channel of [0, 1] as const) {
      expect(reference.third[channel], 'no third from a single note').toBeLessThan(0.1 * root);
      for (const note of ['root', 'third', 'fifth'] as const) {
        expect(triad[note][channel], `the triad's ${note}`).toBeGreaterThan(0.5 * root);
        expect(again[note][channel], `the next triad's ${note}`).toBeGreaterThan(0.5 * root);
      }
      expect(triad.second[channel], 'nothing between the notes').toBeLessThan(0.2 * root);
      expect(guardedLevels.third[channel], 'the ended copy is silent').toBeLessThan(0.1 * root);
      expect(guardedLevels.root[channel], 'the owner plays on').toBeGreaterThan(0.5 * root);
      expect(guardedLevels.fifth[channel], 'the other copy plays on').toBeGreaterThan(0.5 * root);
    }
    expect(Math.abs(f1 - f0), 'two keys, two pitches').toBeGreaterThan(20);
    expect(hiddenReport.both.active, 'the second key took no helper').toBe(1);
    expect(hiddenReport.both.hidden >>> 24, 'it plays on hidden voice 1 of track 1').toBe(0xc6);
    expect((hiddenReport.both.hidden >>> 17) & 1, 'its gate is open').toBe(1);
    expect(hiddenReport.pair.f0, 'both keys sound').toBeGreaterThan(0.5 * root);
    expect(hiddenReport.pair.f1).toBeGreaterThan(0.5 * root);
    expect(hiddenReport.after9.f0, 'the first key up: its note fades').toBeLessThan(0.1 * root);
    expect(hiddenReport.after9.f1, 'the second plays on').toBeGreaterThan(0.5 * root);
    expect(hiddenReport.tail.f1, 'both keys up: the track releases').toBeLessThan(0.1 * root);
    expect(hiddenReport.pair.finite && hiddenReport.after9.finite && hiddenReport.tail.finite).toBe(true);
    expect(hiddenReport.faults, 'only the two copies the test broke').toBe(guard.faults);
    expect(hiddenReport.released.gate, 'the hidden key up closes its gate').toBe(0);
    expect(hiddenReport.released.velocity, 'at its own velocity').toBe(100);
    expect(Math.abs(hiddenReport.released.factor - 1), 'so its fade keeps its level').toBeLessThan(1e-3);
    expect(hmaskIdle & 1, 'track 1 left the hidden path once its notes ended').toBe(0);
    expect(vmaskIdle & 3, 'and the chord path (no copies)').toBe(0);
    expect(limit, 'the default limit').toBe(560000);
    expect(skips, 'the starved block ended the copies of both voices').toBeGreaterThanOrEqual(4);
    for (const channel of [0, 1] as const) {
      expect(stockAfterHidden.root[channel], 'a stock note after the hidden voices plays').toBeGreaterThan(0.5 * root);
      expect(stockAfterChord.root[channel], 'a stock note after the chord plays').toBeGreaterThan(0.5 * root);
      expect(stockAfterChord.third[channel], 'as one note').toBeLessThan(0.1 * root);
      expect(stockAfterChord.fifth[channel]).toBeLessThan(0.1 * root);
      expect(starved.root[channel], 'the owner plays without a budget').toBeGreaterThan(0.5 * root);
      expect(starved.third[channel], 'no copy without a budget').toBeLessThan(0.1 * root);
      expect(starved.fifth[channel]).toBeLessThan(0.1 * root);
      for (const note of ['root', 'third', 'fifth'] as const)
        expect(restored[note][channel], `the chord's ${note} with the limit back`).toBeGreaterThan(0.5 * root);
    }
    expect(even.owner, "the owner's velocity, from the frame").toBe(100);
    expect(even.velocity, "the note's velocity, from its word").toBe(100);
    expect(soft.velocity).toBe(40);
    expect(loud.owner).toBe(60);
    expect(loud.factor, 'a softer owner: the louder key is scaled up').toBeGreaterThan(1);
    expect(Math.abs(even.factor - 1), 'equal velocities: factor 1').toBeLessThan(1e-3);
    expect(even.level, 'the hidden voice sounds').toBeGreaterThan(0.25 * root);
    expect(Math.abs(soft.level / even.level / soft.factor - 1), 'a softer key is softer by its factor').toBeLessThan(0.15);
    expect(Math.abs(loud.level / even.level - 1), "a key sounds at its own velocity whatever the owner's").toBeLessThan(0.1);
    if (soft.curve !== 0) expect(soft.factor, 'the curve gives velocity 40 less than 100').toBeLessThan(0.9);
    expect(even.finite && soft.finite && loud.finite).toBe(true);
    await session.expectNoFault('stock chords');
  });
});
