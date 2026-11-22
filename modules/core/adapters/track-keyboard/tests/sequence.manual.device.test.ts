import { describeOn, liveOptions } from '@digi/regression/runtime/devices.ts';
import { acknowledgedPanel } from '@digi/regression/runtime/panel-ack.ts';
import { trackType } from '@digi/regression/runtime/project-kit.ts';
import { expectRuntimeUi } from '@digi/regression/runtime/ready.ts';
import { captureRemoteOled } from '@digi/regression/runtime/remote.ts';
import { planSymbol } from '@digi/regression/runtime/runtime-plan.ts';
import { prepareRuntimePlanModulesOffline } from '@digi/regression/runtime/runtime-plan-modules.ts';
import { emulatedClockMs, pollEmulated } from '@digi/regression/runtime/waits.ts';
import { withSession } from '@digi/regression/support.ts';
import { expect, test } from 'vitest';

describeOn('dt2', 'shared arpeggiator stock sequence', () => {
  const handle = withSession({ ...liveOptions('dt2', { fresh: true }), dsp: 'sharc', restart: false });
  test('replays audio and MIDI trigs and captures a step-recorded chord', { timeout: 360_000 }, async () => {
    const { session } = handle;
    const image = await prepareRuntimePlanModulesOffline(
      session.options,
      session.runDir,
      session.runDir,
      [
        'DKRT_BOOT',
        'tk_sequence_audio_hits',
        'tk_sequence_audio_locked_hits',
        'tk_sequence_midi_hits',
        'tk_sequence_midi_info',
        'tk_native_audio_active',
        'retrig_keys',
        'tk_capture_committed',
        'tk_capture_last_count',
      ],
      { modules: ['core/track-keyboard', 'core/instrument-play', 'core/digikit-menu', 'core/health'] },
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
    const { edge, tap, turn } = acknowledgedPanel(session);
    const modified = async (modifier: string, key: string | number) => {
      await edge(modifier, true);
      try {
        await tap(key);
      } finally {
        await edge(modifier, false);
      }
    };
    await tap('NO'); // Dismiss the asynchronous stock filesystem acknowledgement.
    await modified('TRK', 'TRIG 1');

    await tap('RECORD'); // GRID
    await tap('TRIG 1');
    await tap('TRIG 5');
    await tap('FLTR');
    for (const trig of ['TRIG 1', 'TRIG 5']) {
      await edge(trig, true);
      try {
        await turn(1, -12);
      } finally {
        await edge(trig, false);
      }
    }
    await tap('RECORD');
    await tap(1);
    await tap(1);
    await tap(1);
    await turn(1, 12);
    await turn(3, 30);

    await modified('TRK', 'TRIG 3');
    await tap('SRC');
    await tap('RECORD');
    for (const trig of ['TRIG 1', 'TRIG 5', 'TRIG 9', 'TRIG 13']) await tap(trig);
    await tap('RECORD');
    for (let page = 0; page < 3; page++) await tap(1);
    await turn(1, 12);
    await turn(3, 30);
    await modified('TRK', 'TRIG 1');

    const symbol = (name: string) => session.client.memPeek(planSymbol(image.plan, name));
    const audioArps = async () => ((await symbol('tk_native_audio_active')) ?? 0) & 5;
    const held = async () => (await audioArps()) & 1;
    const hits = () => symbol('tk_sequence_audio_hits');
    expect(await hits()).toBe(0);
    await tap('PLAY');
    await pollEmulated(session, hits, (n) => n !== null && n > 0, {
      emulatedMs: 5000,
      message: 'the due native note was admitted to the shared scheduler',
    }).catch(async (error) => {
      await captureRemoteOled(session, 'sequence-first-due-failure');
      throw error;
    });
    await pollEmulated(session, held, (n) => n === 1, {
      emulatedMs: 5000,
      intervalMs: 10,
      message: 'the native audio backend received a generated note-on',
    });
    // Stock DSP-frame parameter-lock masks: three words per audio track.
    const nativeLock = async () =>
      ((await session.client.memPeek(0x47db461c)) ?? 0) |
      ((await session.client.memPeek(0x47db4620)) ?? 0) |
      ((await session.client.memPeek(0x47db4624)) ?? 0);
    await pollEmulated(session, nativeLock, (mask) => mask !== 0, {
      emulatedMs: 5000,
      intervalMs: 10,
      message: 'stock audio dispatch installed the generated note’s parameter lock',
    });
    expect(await symbol('tk_sequence_audio_locked_hits')).toBeGreaterThan(0);

    // Set up the stock performance mode in this disposable emulated project.
    // Project +0x30 embeds settings; its getter returns the pointer at +0x10.
    // The actual retrig press and release still go through the native panel.
    const project = (await session.client.memPeek(0x44f370a0))!;
    const settings = (await session.client.memPeek(project + 0x40))!;
    const selectTrigMode = async (wanted: number) => {
      const addr = settings + 0x118;
      const bytes = Buffer.alloc(4);
      bytes.writeUInt32BE(wanted);
      await session.client.call('mem.cf.write', { addr, hex: bytes.toString('hex') });
      expect(await session.client.memPeek(addr), 'stock performance mode').toBe(wanted);
    };
    await selectTrigMode(2); // RETRIGS
    expect(await session.client.memPeek(settings + 0x38), 'stock KEYBOARD mode').toBe(0);
    await pollEmulated(session, audioArps, (mask) => mask === 5, {
      emulatedMs: 5000,
      intervalMs: 20,
      message: 'both audio tracks are arpeggiating before the manual gesture',
    });
    await edge('TRIG 9', true);
    try {
      const keysAddress = planSymbol(image.plan, 'retrig_keys');
      const retrigKeys = async () => {
        const word = await session.client.memPeek(keysAddress & ~3);
        return word === null ? -1 : (word >>> (16 - (keysAddress & 2) * 8)) & 0xffff;
      };
      await pollEmulated(session, retrigKeys, (mask) => mask === 1 << 8, {
        emulatedMs: 1000,
        message: 'native RETRIGS claimed the original physical key',
      }).catch(async (error) => {
        await captureRemoteOled(session, 'sequence-retrig-owner-failure');
        throw error;
      });
      await pollEmulated(session, held, (n) => n === 0, {
        emulatedMs: 500,
        message: 'the owner’s previously dispatched arp note retired',
      });
      const until = emulatedClockMs(await session.status()) + 2100;
      let otherPlayed = false;
      await pollEmulated(
        session,
        async () => {
          const mask = await audioArps();
          expect(mask & 1, 'shared arp dispatched a note on the manual retrig owner').toBe(0);
          otherPlayed ||= (mask & 4) !== 0;
          return emulatedClockMs(await session.status());
        },
        (now) => now >= until,
        {
          emulatedMs: 3000,
          intervalMs: 20,
          message: 'hold manual RETRIGS across two sequencer seeds',
        },
      );
      expect(otherPlayed, 'the other track still arpeggiated during manual RETRIGS').toBe(true);
    } finally {
      await edge('TRIG 9', false);
    }
    await pollEmulated(session, held, (n) => n === 1, {
      emulatedMs: 5000,
      intervalMs: 20,
      message: 'releasing the original retrig key restores its track arp',
    });
    await tap('STOP');
    await pollEmulated(session, held, (n) => n === 0, {
      emulatedMs: 5000,
      message: 'STOP released the sequencer-owned note',
    });
    await selectTrigMode(0); // TRACKS
    await edge('RECORD', true);
    try {
      await tap('STOP');
    } finally {
      await edge('RECORD', false);
    }
    await edge('KEYBOARD', true);
    const notes = ['TRIG 9', 'TRIG 11', 'TRIG 13'];
    try {
      for (const note of notes) await edge(note, true);
      await pollEmulated(session, () => symbol('tk_capture_last_count'), (n) => n === 3, {
        emulatedMs: 5000,
        intervalMs: 10,
        message: 'three held step-entry keys committed to one ordered native trig recipe',
      });
    } finally {
      for (const note of notes) await edge(note, false);
      await edge('KEYBOARD', false);
    }
    expect(await symbol('tk_capture_committed')).toBeGreaterThanOrEqual(3);
    await tap('RECORD');

    await modified('TRK', 'TRIG 2');
    await modified('FUNC', 'SRC');
    for (let i = 0; i < 6; i++) await tap('DOWN');
    await tap('YES');
    await pollEmulated(session, () => trackType(session, 2), (type) => type === 5, {
      emulatedMs: 5000,
      message: 'track 2 uses the stock MIDI machine',
    });
    await tap('SRC');
    const channelEnabled = async () => ((await session.client.memPeek(0x44e3b9e8)) ?? 0) & 1;
    if (!(await channelEnabled())) await modified('FUNC', 'PUSH A');
    await turn(1, 4);
    expect(await channelEnabled(), 'MIDI track CHAN enabled').toBe(1);
    await tap('RECORD');
    await tap('TRIG 1');
    await tap('TRIG 5');
    await tap('RECORD');
    await tap(1);
    await tap(1);
    await tap(1);
    await turn(1, 12);
    await turn(3, 30);

    const outgoing = async () => {
      const response = await session.client.call<{ hex: string }>('midi.out', {});
      return [...Buffer.from(response.hex.replaceAll(/\s/g, ''), 'hex')];
    };
    await outgoing();
    await tap('PLAY');
    try {
      await pollEmulated(session, () => symbol('tk_sequence_midi_hits'), (n) => n !== null && n > 0, {
        emulatedMs: 5000,
        message: 'the due stock MIDI note reached the shared scheduler',
      });
    } catch (error) {
      const base = planSymbol(image.plan, 'tk_sequence_midi_info');
      const counters = await Promise.all([0, 4, 8, 12, 16].map((offset) => session.client.memPeek(base + offset)));
      throw new Error('Native MIDI scheduled/due/matched/enabled/decoded ' + JSON.stringify(counters), { cause: error });
    }
    const wire: number[] = [];
    let active: [number, number] | undefined;
    await pollEmulated(session, async () => {
      wire.push(...await outgoing());
      for (let i = 0; i + 2 < wire.length; i++) {
        const [status, note, velocity] = wire.slice(i, i + 3) as [number, number, number];
        if ((status & 0xf0) === 0x90 && note < 128 && velocity > 0 && velocity < 128) {
          active = [status & 15, note];
        }
      }
      return active;
    }, (note) => note !== undefined, { emulatedMs: 5000, message: 'native MIDI route emitted a note-on' });
    await tap('STOP');
    await pollEmulated(session, async () => {
      wire.push(...await outgoing());
      return wire.some((status, i) =>
        (status & 15) === active![0] && wire[i + 1] === active![1] &&
        ((status & 0xf0) === 0x80 || ((status & 0xf0) === 0x90 && wire[i + 2] === 0)));
    }, Boolean, { emulatedMs: 5000, message: 'native MIDI route emitted the paired note-off' });

    // Switch from the sequencer arp to the held keyboard path. Change the
    // stock output mask while a generated note is still held on each port.
    await turn(1, -12);
    await tap(1);
    await turn(1, 12);
    await tap('KEYBOARD');
    const portMask = 0x44e3b9e0;
    const initialMask = await session.client.memPeek(portMask);
    expect(initialMask).not.toBeNull();
    const setPorts = async (mask: number) => {
      const bytes = Buffer.alloc(4);
      bytes.writeUInt32BE(mask);
      await session.client.call('mem.cf.write', { addr: portMask, hex: bytes.toString('hex') });
    };
    const outgoingUsb = async () => {
      const response = await session.client.call<{ hex: string }>('midi.usb.out', {});
      return [...Buffer.from(response.hex.replaceAll(/\s/g, ''), 'hex')];
    };
    const noteOn = (bytes: number[]) => {
      const offset = bytes.findIndex((status, i) =>
        (status & 0xf0) === 0x90 && bytes[i + 1] < 128 && bytes[i + 2] > 0 && bytes[i + 2] < 128);
      return offset < 0 ? undefined : { channel: bytes[offset] & 15, pitch: bytes[offset + 1] };
    };
    const noteOff = (bytes: number[], note: { channel: number; pitch: number }) =>
      bytes.some((status, i) => (status & 15) === note.channel && bytes[i + 1] === note.pitch &&
        ((status & 0xf0) === 0x80 || ((status & 0xf0) === 0x90 && bytes[i + 2] === 0)));
    try {
      for (const selected of [1, 2]) {
        const read = selected === 1 ? outgoing : outgoingUsb;
        const readOther = selected === 1 ? outgoingUsb : outgoing;
        await setPorts(selected);
        await read();
        await readOther();
        await edge('TRIG 9', true);
        let note;
        try {
          note = await pollEmulated(session, async () => noteOn(await read()), (n) => n !== undefined, {
            emulatedMs: 3000,
            message: 'the generated key used its selected physical output',
          });
          await setPorts(selected ^ 3);
        } finally {
          await edge('TRIG 9', false);
        }
        await pollEmulated(session, async () => noteOff(await read(), note!), Boolean, {
          emulatedMs: 3000,
          message: 'the paired release stayed on the original output',
        });
        expect(noteOff(await readOther(), note!)).toBe(false);
      }
    } finally {
      await setPorts(initialMask ?? 0);
      await tap('KEYBOARD');
    }
    await session.expectNoFault('shared arpeggiator stock sequence');
  });
});
