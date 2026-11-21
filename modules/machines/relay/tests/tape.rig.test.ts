import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect, it } from 'vitest';
import { RawRig, f2w, w2f } from '@digi/sharc-rig-kit';

it('prints spaced stereo echoes, punches immediately, parks, reverses and undoes on the real tape path', async () => {
  const root = path.resolve(import.meta.dirname, '../../../..');
  const adapter = 'modules/core/adapters/relay-track/src/dsp';
  const files = [
    'modules/core/adapters/relay-storage/src/dsp/storage.s',
    'modules/core/adapters/relay-storage/src/dsp/pool.s',
    'modules/core/adapters/relay-track/src/dsp/audio.s',
    adapter + '/engine.s',
    'modules/machines/relay/src/dsp/voice.s',
  ];
  const ext = 0x20110000,
    cmd = ext + 512,
    status = cmd + 256,
    timing = status + 768,
    ret = 0xb9f100;
  const rig = await RawRig.open({
    objects: [
      ...files.map((f, i) => ({
        text: readFileSync(path.join(root, f), 'utf8').replaceAll('\r\n', '\n'),
        includes: [path.join(root, 'sdk/dsp/contract'), path.join(root, adapter)],
        base: 0xb90000 + i * 0x2000,
      })),
      {
        text: '.SECTION/PM seg_pmco;\n.GLOBAL _tape_test_read;\n_tape_test_read: I0 = R4; R0 = DM(0,I0); RTS;',
        base: 0xb9c000,
      },
    ],
    externs: {
      _relay_external: ext,
      _rtm_command: cmd,
      _relay_status: status,
      _relay_request: status + 256,
      _relay_response: status + 512,
      _relay_timing: timing,
      _pattern_position_info: status + 1024,
      _rtm_bus: ext + 4096,
      _rtm_mod_frame: ext + 12288,
    },
    returnSw: ret,
  });
  const track = 3,
    tape = rig.data._rtm_tapes! / 4 + track * 128,
    row = rig.data._rtm_tracks! / 4 + track * 16;
  const modFrame = (ext + 12288) / 4;
  function setLanes(values: number[]) {
    const params = modFrame + 24 * track;
    const packed = (low: number, high: number) => ((high << 16) | low) >>> 0;
    rig.poke(params + 55, packed(values[0]!, values[1]!));
    rig.poke(params + 56, packed(values[2]!, 0));
    rig.poke(params + 57, packed(0, values[3]!));
    rig.poke(params + 58, packed(values[4]!, values[5]!));
  }
  let sequence = 0;
  function input(hit = false) {
    for (let i = 0; i < 32; ++i) {
      rig.poke(ext / 4 + i, f2w(hit && !i ? 0.5 : 0));
      rig.poke(ext / 4 + 32 + i, f2w(hit && !i ? -0.25 : 0));
    }
  }
  async function block(op = 0, source = 0, rate = 0, arg = 0, end = 0) {
    [0, ++sequence, op, track, source, rate, arg, end].forEach((v, i) => rig.poke(cmd / 4 + i, v >>> 0));
    const b = await rig.call(
      '_rtm_block',
      { M6: 1, M5: 0 },
      {
        maxSteps: 300000,
        reads: [
          [status / 4, 14],
          [tape + 32, 64],
        ],
      },
    );
    expect(
      b.halt,
      'command ' +
        op +
        ' at ' +
        b.final_pc.toString(16) +
        ' ' +
        Object.entries(rig.symbols)
          .filter(([, v]) => v <= b.final_pc)
          .sort((a, b) => b[1] - a[1])[0],
    ).toBe('breakpoint');
    const words = [...b.reads[0]!];
    const tag = (words[3]! >>> 24) | ((words[13]! >>> 16) & 0xff00);
    words[3] = words[3]! & 0xffffff;
    words[13] = words[13]! & 0xffffff;
    const hash = words.slice(1).reduce((acc, word) => (Math.imul(acc, 31) + word) >>> 0, 0);
    expect(tag).toBe((hash ^ (hash >>> 16)) & 65535);
    expect(words[13] & 255).toBe(0);
    return { status: b.reads[0]!, audio: b.reads[1]!.map((w) => w2f(w ?? 0)) };
  }
  const controls = (speed: number, head: number, feedback = 50, mix = 100) =>
    block(11, (100 << 5) | (feedback << 12) | (mix << 19), speed, head);
  async function reset() {
    const state = ext + 2048,
      abi = ext + 2304;
    rig.poke(abi / 4 + 4, track);
    const r = await rig.call('_relay_note_on', { R4: state, R8: abi, M13: 0, I13: ret });
    expect(r.halt).toBe('breakpoint');
  }
  try {
    for (let i = 0; i < 480; ++i) rig.poke(ext / 4 + i, 0);
    rig.poke(timing / 4 + 1, 512);
    input();
    await controls(65536, 4096);
    input(true);
    let b = await block(1, 0, 0, 512, 16);
    expect(b.status[5]).toBe(512);
    input();
    for (const amp of [0.5, 0.25, 0.125]) {
      b = await block();
      expect(b.audio[0]).toBeCloseTo(amp, 5);
      expect(b.audio[32]).toBeCloseTo(-amp / 2, 5);
    }
    for (let i = 4; i < 16; ++i) b = await block();
    expect(b.status[2]! & 15).toBe(0);
    expect(b.status[11]! >>> 8).toBe(16);
    // A panel launch binds the voice first. A played note is an independent reset.
    await reset();
    await controls(65536, 65536, 0);
    await reset();
    b = await block();
    expect(b.status[6]).toBe(32);
    input(true);
    await block(1);
    input();
    b = await block(2);
    expect(b.status[5]).toBe(512);
    expect(b.status[11]! & 1).toBe(1);
    b = await block(4);
    expect(b.status[10]! & 1).toBe(1);
    b = await controls(0, 65536);
    const parked = b.status[6];
    b = await block();
    expect(b.status[6]).toBe(parked);
    expect(b.audio.every((x) => x === 0)).toBe(true);
    b = await controls(-32768, 65536);
    expect(b.status[6]).toBe((parked + 512 - 16) % 512);
    // A dry monitor is immediate even when the motor is parked.
    input(true);
    b = await controls(0, 65536, 50, 0);
    expect(b.audio[0]).toBeCloseTo(0.5, 5);
    expect(b.audio[32]).toBeCloseTo(-0.25, 5);
    expect(await rig.peek(row, 1)).toEqual([512]);
    // Read the stored native PCM, independent of the dry/wet monitor.
    async function sample(frame: number, slot = 9 + track) {
      const p = await rig.call('_relay_pool', { R4: 4, R8: slot, R12: frame, M6: 1 }, { readRegs: ['R0'] });
      expect(p.halt).toBe('breakpoint');
      const value = await rig.call('_tape_test_read', { R4: p.regs.R0! >>> 0 }, { readRegs: ['R0'] });
      expect(value.halt).toBe('breakpoint');
      return value.regs.R0! >>> 0;
    }
    expect(await sample(0)).toBe(0x4000e000);
    expect(await sample(32)).toBe(0x2000f000);
    // Move the native position destination by 1/8 lap (64 frames). A
    // known stereo impulse must arrive 64 samples earlier at the output.
    await block(14);
    rig.poke(modFrame + 37 + (track >> 1), 21 << 16);
    setLanes([0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4800]);
    await block(11, (21 << 26) | (100 << 5) | (100 << 19), 65536, 65536);
    await block(18, 0, 0, 0, 512);
    await block(16, 0, 0, -80, 512);
    input();
    b = await block(3);
    expect(b.audio[16]).toBeCloseTo(0.5, 5);
    expect(b.audio[48]).toBeCloseTo(-0.25, 5);
    await reset();
    // Play the recorded impulse from an independently moved reader. Placing
    // the impulse 16 samples after launch also clears the seek crossfade.
    await controls(65536, 65536, 0);
    await block(14);
    b = await block(18, 0, 0, 0, 512);
    const originalWriter = b.status[6]!;
    await block(16, 0, 0, -16, 512);
    b = await block(17, 0, 0, 32, 512);
    expect(b.status[6]).toBe((originalWriter + 32) % 512);
    input();
    b = await block(3);
    expect(b.audio[16]).toBeCloseTo(0.5, 5);
    expect(b.audio[48]).toBeCloseTo(-0.25, 5);
    // Acknowledge the panel launch before testing the next musical reset.
    await reset();
    // At zero distance the wet monitor hears the frame just written.
    input();
    await controls(131072, 0, 0);
    await reset();
    input(true);
    b = await block(1);
    input();
    await block(2);
    expect(await sample(0)).toBe(0x4000e000);
    expect(await sample(1), '2x writes the skipped physical cell').toBe(0x4000e000);
    expect(await sample(100), 'frames beyond the punch remain intact').toBe(0);
    await block(4);
    expect(await sample(1), 'undo restored the pre-punch frame').toBe(0);
    // Negative 2x fills the cell before frame zero on wrap.
    await controls(-131072, 0, 0);
    await reset();
    input(true);
    await block(1);
    input();
    await block(2);
    expect(await sample(511)).toBe(0x4000e000);
    await block(4);
    // Prove endpoint causality on an already running voice with no reset fade.
    await controls(65536, 0, 0);
    input();
    await block();
    input(true);
    b = await block(1);
    expect(b.audio[0]).toBeCloseTo(0.5, 5);
    expect(b.audio[32]).toBeCloseTo(-0.25, 5);
    // Stop a live punch after a sustained stereo note. The final wet block fades to zero.
    for (let i = 0; i < 32; ++i) {
      rig.poke(ext / 4 + i, f2w(0.5));
      rig.poke(ext / 4 + 32 + i, f2w(-0.25));
    }
    await block();
    input();
    b = await block(3);
    expect(b.status[7]! & 1).toBe(0);
    expect(b.audio[0]).toBeCloseTo(0.5, 5);
    expect(b.audio[8]).toBeCloseTo(0.25, 5);
    expect(b.audio[16]).toBe(0);
    expect(b.audio[40]).toBeCloseTo(-0.125, 5);
    const clock = (status + 1024) / 4;
    rig.poke(clock, 0x50504f53);
    await reset();
    b = await block(13);
    expect(b.status[7]! & 1).toBe(0);
    const stopped = b.status[6];
    b = await block();
    expect(b.status[6]).toBe(stopped);
    rig.poke(clock + 5, 3);
    b = await block();
    expect(b.status[7]! & 1).toBe(1);
    expect(b.status[6]).toBe(32);
    b = await block(15);
    expect(b.status[2]! & 15).toBe(2);
    rig.poke(clock, 0);
    rig.poke(clock + 5, 0);
    b = await block(13);
    expect(b.status[2]! & 15).toBe(2);
    rig.poke(clock, 0x50504f53);
    b = await block();
    expect(b.status[2]! & 15).toBe(0);
    expect(b.status[7]! & 1).toBe(0);
    // Stopped seeks change one head at a time, including across frame zero.
    const writer = b.status[6]!;
    const slip = (s: number[]) => (s[2]! >>> 16) | ((s[4]! & 0xff00) << 8);
    b = await block(16, 0, 0, -32, 512);
    expect(b.status[6]).toBe(writer);
    expect(slip(b.status)).toBe(480);
    b = await block(17, 0, 0, 64, 512);
    expect(b.status[6]).toBe((writer + 64) % 512);
    expect(slip(b.status)).toBe(416);
    b = await block(18, 0, 0, 0, 512);
    expect((b.status[6]! + slip(b.status)) % 512).toBe(0);
    expect(b.status[7]! & 1).toBe(0);
    await block(3);
    b = await block(14);
    expect(b.status[7]! & 1).toBe(0);
    // All six mirrors come from this track of the completed native frame.
    rig.poke(modFrame + 37 + (track >> 1), 21 << 16);
    setLanes(Array(6).fill(0x4000));
    await block(11, (21 << 26) | (50 << 5) | (50 << 12) | (50 << 19), 0, 32768);
    await block();
    const effective = () => rig.peek(tape + 19, 6);
    const neutral = await effective();
    expect(neutral).toEqual([0, 32768, f2w(0.5), f2w(0.5), f2w(0.5), 0]);
    setLanes([0x5000, 0x3000, 0x6000, 0x2000, 0x4800, 0x3800]);
    input(true);
    b = await block();
    expect(await effective()).toEqual([65536, 16384, f2w(1), 0, f2w(0.625), f2w(-0.125)]);
    expect(b.audio[0]).toBeCloseTo(0.3125, 5);
    expect(b.audio[32]).toBeCloseTo(-0.15625, 5);
    // A foreign type cannot borrow modulation. The first returning frame is also neutral.
    rig.poke(modFrame + 37 + (track >> 1), 9 << 16);
    await block();
    expect(await effective()).toEqual(neutral);
    rig.poke(modFrame + 37 + (track >> 1), 21 << 16);
    await block();
    expect(await effective()).toEqual(neutral);
    setLanes(Array(6).fill(0x4000));
    await block();
    expect(await effective()).toEqual(neutral);
  } finally {
    await rig.close();
  }
}, 90000);
