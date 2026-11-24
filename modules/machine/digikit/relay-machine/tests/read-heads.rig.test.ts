import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect, it } from 'vitest';
import { RawRig, f2w, w2f } from '@digi/sharc-rig-kit';

it('plays two read-only RELAY heads from the leased owner tape at independent pitches', async () => {
  const root = path.resolve(import.meta.dirname, '../../../..');
  const read = (file: string) => readFileSync(path.join(root, file), 'utf8').replaceAll('\r\n', '\n');
  const files = [
    'image-modules/adapters/relay-storage/src/dsp/storage.s',
    'image-modules/adapters/relay-storage/src/dsp/pool.s',
    'image-modules/adapters/track-keyboard/src/dsp/track-pool.s',
    'modules/machine/digikit/relay-machine/src/dsp/voice.s',
  ];
  const helper = read('image-modules/points/dsp-voice/src/dsp/dsp-voice.s');
  const exp2 = helper.slice(helper.indexOf('.GLOBAL _dv_exp2;'), helper.indexOf('// R0 = the unsigned 16-bit'));
  const engine = read('image-modules/adapters/relay-track/src/dsp/engine.s');
  const ampDone = engine.slice(engine.indexOf('.GLOBAL _rtm_group_amp_done;'), engine.indexOf('.GLOBAL _rtm_block;'));
  const pcmMemory = 0x20110000;
  const tracks = pcmMemory + 0x1000;
  const tapes = tracks + 0x1000;
  const input = pcmMemory + 0x3000;
  const left = input + 0x100;
  const right = left + 0x100;
  const lead = right + 0x100;
  const second = lead + 0x100;
  const rx = pcmMemory + 0x4000;
  const ampPointer = pcmMemory + 0x5000;
  const ampRecords = ampPointer + 0x100;
  const ret = 0xb9f100;
  const rig = await RawRig.open({
    objects: [
      ...files.map((file, i) => ({
        text: read(file),
        includes: [path.join(root, 'sdk/dsp/contract')],
        base: 0xb90000 + i * 0x2000,
      })),
      {
        text: `.SECTION/PM seg_pmco;
.EXTERN _tk_pool_head_owner;
.EXTERN _relay_storage_info;
.EXTERN _mi_envelope_records;
.GLOBAL _rtm_read_head_owner;
_rtm_read_head_owner: JUMP _tk_pool_head_owner;
.GLOBAL _relay_test_root;
_relay_test_root:
    I3 = _relay_storage_info;
    R0 = DM(2,I3);
    R1 = LSHIFT R4 BY 2;
    R0 = R0 + R1;
    I3 = R0;
    R0 = DM(8,I3);
    RTS;
.GLOBAL _relay_test_backing;
_relay_test_backing:
    I3 = R4;
    R0 = DM(5,I3);
    R1 = DM(6,I3);
    RTS;
.GLOBAL _relay_test_pcm;
_relay_test_pcm:
    I3 = R4;
    R0 = DM(0,I3);
    RTS;
${exp2}
${ampDone}`,
        base: 0xb9a000,
      },
    ],
    externs: { _rtm_tracks: tracks, _rtm_tapes: tapes, _mi_envelope_records: ampPointer },
    returnSw: ret,
  });
  const owner = 3;
  const supportingTrack = 9;
  const row = tracks / 4 + owner * 16;
  const helperRow = tracks / 4 + supportingTrack * 16;
  const length = 64;
  const ownerSlot = 9 + owner;

  async function pool(R4: number, R8 = 0, R12 = 0) {
    const result = await rig.call('_relay_pool', { R4, R8, R12, M6: 1 }, { readRegs: ['R0'] });
    expect(result.halt).toBe('breakpoint');
    expect(result.regs.R0! >>> 0).not.toBe(0xffffffff);
    return result.regs.R0! >>> 0;
  }
  // Read metadata through DSP byte-addressed loads, using the pool's
  // slot table and PARENT/UNSEEDED layout.
  async function committedRoot() {
    const result = await rig.call('_relay_test_root', { R4: ownerSlot }, { readRegs: ['R0'] });
    expect(result.halt).toBe('breakpoint');
    expect(result.regs.R0).not.toBeNull();
    return result.regs.R0! >>> 0;
  }
  async function backing(R4: number) {
    const result = await rig.call('_relay_test_backing', { R4 }, { readRegs: ['R0', 'R1'] });
    expect(result.halt).toBe('breakpoint');
    expect(result.regs.R0).not.toBeNull();
    expect(result.regs.R1).not.toBeNull();
    return [result.regs.R0! >>> 0, result.regs.R1! >>> 0];
  }
  async function note(state: number, track: number, pitch: number) {
    rig.poke(input / 4, pitch << 8);
    rig.poke(input / 4 + 4, track);
    const result = await rig.call('_relay_note_on', { R4: state, R8: input, M13: 0, I13: ret });
    expect(result.halt).toBe('breakpoint');
  }
  async function render(state: number, status = 2, gate = 1) {
    rig.poke(input / 4 + 4, state === lead ? owner : supportingTrack);
    rig.poke(input / 4 + 6, gate);
    const result = await rig.call(
      '_relay_render',
      { R4: state, R8: input, M13: 0, I13: ret, M6: 1 },
      { reads: [[left / 4, 32], [right / 4, 32]], readRegs: ['R0'] },
    );
    expect(result.halt).toBe('breakpoint');
    expect(result.regs.R0).toBe(status);
    return result.reads.map((words) => words.map((word) => w2f(word ?? 0) * 65536));
  }
  const expected = (from: number, stride: number) =>
    [1, -1].map((sign) => Array.from({ length: 32 }, (_, i) => sign * ((from + stride * i) % length + 1)));
  const silence = [Array(32).fill(0), Array(32).fill(0)];
  const amp = (track: number, phase: number, release: number, gain: number) => {
    const record = (ampRecords + 88 * track) / 4;
    rig.poke(record + 4, release);
    rig.poke(record + 7, phase);
    rig.poke(record + 10, f2w(-gain));
  };

  try {
    for (const trackRow of [row, helperRow]) {
      for (let i = 0; i < 16; ++i) rig.poke(trackRow + i, 0);
    }
    rig.poke(tapes / 4 + owner * 128, 0);
    await pool(0);
    await pool(2);
    for (let i = 1; i <= length; ++i) await pool(3, ((i << 16) | (-i & 0xffff)) >>> 0);
    await pool(5, ownerSlot);
    [length, 21, ownerSlot, 1].forEach((value, i) => rig.poke(row + i, value));
    rig.poke(helperRow + 1, 51);
    rig.poke(input / 4 + 3, left);
    rig.poke(input / 4 + 5, right);
    const low = 0xa << (owner * 4);
    const high = owner << ((supportingTrack - 8) * 4);
    const mask = 0x544b0000 | (1 << supportingTrack);
    async function publish(marker: number, group = true, tag = 0, corrupt = false) {
      const header = (group ? mask : 0x544b0000) ^ (tag << 16);
      const nextHigh = group ? high : 0;
      const packet = [header, marker, nextHigh, header ^ marker ^ nextHigh ^ 0x22dca71b ^ Number(corrupt)];
      packet.forEach((word, i) => rig.poke((rx + 0x854) / 4 + i, word));
      expect((await rig.call('_tk_pool_receive', { I3: rx })).halt).toBe('breakpoint');
    }
    async function headOwner(track: number) {
      const lookup = await rig.call('_tk_pool_head_owner', { R1: track }, { readRegs: ['R0'] });
      expect(lookup.halt).toBe('breakpoint');
      return lookup.regs.R0! >>> 0;
    }
    async function expectEcho(tag: number) {
      const serial = 23;
      const quiet = 0x1234;
      rig.poke(rig.data._tk_pool_quiet_serial! / 4, serial);
      rig.poke(rig.data._tk_pool_quiet_mask! / 4, quiet);
      const reply = await rig.call('_tk_pool_quiet_reply', { I4: rx }, { reads: [[rx / 4 + 24, 2]] });
      expect(reply.halt).toBe('breakpoint');
      expect(reply.reads[0]).toEqual([
        (quiet << 16) | (0x4b51 ^ tag),
        ((serial ^ quiet ^ 0x32f7 ^ tag) << 16) | serial,
      ]);
    }
    await publish(0);
    for (const track of [owner, supportingTrack]) {
      expect(await headOwner(track)).toBe(0xffffffff); // A stock pool cannot authorize a RELAY Sound.
    }

    await publish(low, false, 0xa5);
    expect(await headOwner(owner)).toBe(owner);
    expect(await headOwner(supportingTrack)).toBe(0xffffffff);
    await expectEcho(0xa5);
    await note(lead, owner, 72);
    expect(await render(lead)).toEqual(expected(0, 2)); // AUTO solo starts a pitched head.
    expect(await rig.peek(row + 1, 1)).toEqual([21]); // The motor did not seek.
    await publish(0, false, 0xb2, true);
    expect(await headOwner(owner)).toBe(owner);
    await expectEcho(0xa5);
    await publish(0, false, 0xb2);
    expect(await headOwner(owner)).toBe(0xffffffff);
    await expectEcho(0xb2);

    await publish(low);
    await note(lead, owner, 60);
    await note(second, supportingTrack, 72);
    expect(await render(lead)).toEqual(expected(0, 1));
    expect(await render(second)).toEqual(expected(0, 2));
    expect(await rig.peek(row + 1, 1)).toEqual([21]);
    expect(await rig.peek(helperRow + 1, 1)).toEqual([51]);

    rig.poke(ampPointer / 4, ampRecords);
    amp(owner, 4, 32, 1e-8);
    expect(await render(lead)).toEqual(expected(32, 1)); // Still held.
    amp(supportingTrack, 3, 32, 1e-8);
    expect(await render(second, 2, 0)).toEqual(expected(0, 2)); // Finite AHD hold.
    amp(supportingTrack, 4, 32, 0.25);
    expect(await render(second, 2, 0)).toEqual(expected(0, 2)); // Audible release.
    amp(supportingTrack, 4, 127, 1e-8);
    expect(await render(second, 2, 0)).toEqual(expected(0, 2)); // Infinite release.
    amp(supportingTrack, 4, 32, 1e-8);
    expect(await render(second, 3, 0)).toEqual(silence); // Native cursor can now clear.
    expect(await render(lead)).toEqual(expected(0, 1)); // The sibling is untouched.

    const originalRoot = await committedRoot();
    expect(originalRoot).not.toBe(0);
    await pool(2, length);
    await pool(19, ownerSlot);
    rig.poke(row + 2, 8); // The active writer owns a private slot during a punch.
    expect(await render(lead)).toEqual(expected(32, 1));
    await note(second, supportingTrack, 72);
    expect(await render(second)).toEqual(expected(0, 2));
    expect(await rig.peek(row + 1, 2)).toEqual([21, 8]);
    expect(await committedRoot()).toBe(originalRoot);
    await pool(16, ownerSlot);
    rig.poke(row + 2, ownerSlot);
    rig.poke(row + 8, 1); // Commit or undo invalidates both saved heads.
    expect(await render(lead, 3)).toEqual(silence);
    expect(await render(second, 3)).toEqual(silence);

    const dubRoot = await committedRoot();
    expect(dubRoot).not.toBe(originalRoot);
    expect(await backing(dubRoot)).toEqual([originalRoot, 1]);
    await note(second, supportingTrack, 72);
    expect(await render(second)).toEqual(expected(0, 2));
    expect(await backing(dubRoot)).toEqual([originalRoot, 1]);

    await pool(2, length);
    await pool(19, 0xffffffff);
    await pool(5, ownerSlot);
    rig.poke(row + 8, 2);
    const blankRoot = await committedRoot();
    await note(second, supportingTrack, 72);
    expect(await render(second)).toEqual(silence);
    expect(await backing(blankRoot)).toEqual([0, 1]);

    rig.poke(row + 11, 1);
    await note(second, supportingTrack, 60);
    expect(await rig.peek(row + 11, 1)).toEqual([1]);
    await note(lead, owner, 60);
    expect(await rig.peek(row + 11, 1)).toEqual([0]);

    // 256 is a storage-group split, and the seam at 263 is separate from it.
    const frames = 263;
    const pack = (l: number, r: number) => ((l << 16) | (r & 0xffff)) >>> 0;
    const leftPcm = (i: number) => 13 * i - 1200;
    const rightPcm = (i: number) => 1700 - 7 * i;
    const wave = (from: number, stride: number, sample: (i: number) => number) =>
      Array.from({ length: 32 }, (_, i) => {
        const phase = (from + stride * i + frames) % frames;
        const lo = Math.floor(phase);
        const frac = phase - lo;
        return sample(lo) + frac * (sample((lo + 1) % frames) - sample(lo));
      });
    await pool(2, frames);
    for (let i = 0; i < frames; ++i) {
      expect(await pool(3, pack(leftPcm(i), rightPcm(i)))).toBe(i + 1);
    }
    await pool(5, ownerSlot);
    rig.poke(row, frames);
    rig.poke(row + 8, 3);

    const address = await pool(22, ownerSlot, 250);
    expect(address).not.toBe(0);
    const stored = await rig.call('_relay_test_pcm', { R4: address }, { readRegs: ['R0'] });
    expect(stored.halt).toBe('breakpoint');
    expect(stored.regs.R0! >>> 0).toBe(pack(leftPcm(250), rightPcm(250)));
    for (const [pitch, stride] of [[60, 1], [48, 0.5]] as const) {
      await note(lead, owner, pitch);
      rig.poke(lead / 4 + 1, 250);
      expect(await render(lead)).toEqual([wave(250, stride, leftPcm), wave(250, stride, rightPcm)]);
    }

    const tape = tapes / 4 + owner * 128;
    rig.poke(tape, 1);
    rig.poke(tape + 19, (-32768) >>> 0); // T_EFF_RATE, −½.
    rig.poke(tape + 22, f2w(1)); // T_EFF_MIX.
    await note(lead, owner, 60);
    rig.poke(lead / 4 + 1, 4);
    expect(await render(lead)).toEqual([wave(4, -0.5, leftPcm), wave(4, -0.5, rightPcm)]);
    rig.poke(tape, 0);

    await pool(2, frames);
    await pool(19, 0xffffffff);
    await pool(12, pack(16384, -8192), 255); // Seed one group, leaving its neighbor implicit.
    await pool(5, ownerSlot);
    rig.poke(row + 8, 4);
    const partialRoot = await committedRoot();
    expect(await backing(partialRoot)).toEqual([0, 1]);
    await note(lead, owner, 48);
    rig.poke(lead / 4 + 1, 255);
    expect(await render(lead)).toEqual([
      wave(255, 0.5, (i) => (i === 255 ? 16384 : 0)),
      wave(255, 0.5, (i) => (i === 255 ? -8192 : 0)),
    ]);
    expect(await backing(partialRoot)).toEqual([0, 1]);
  } finally {
    await rig.close();
  }
}, 90_000);
