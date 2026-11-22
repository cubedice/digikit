import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect, it } from 'vitest';
import { RawRig, w2f } from '@digi/sharc-rig-kit';

it('plays two read-only RELAY heads from the leased owner tape at independent pitches', async () => {
  const root = path.resolve(import.meta.dirname, '../../../..');
  const read = (file: string) => readFileSync(path.join(root, file), 'utf8').replaceAll('\r\n', '\n');
  const files = [
    'modules/core/adapters/relay-storage/src/dsp/storage.s',
    'modules/core/adapters/relay-storage/src/dsp/pool.s',
    'modules/core/adapters/track-keyboard/src/dsp/track-pool.s',
    'modules/machines/relay/src/dsp/voice.s',
  ];
  const helper = read('modules/core/adapters/dsp-voice/src/dsp/dsp-voice.s');
  const exp2 = helper.slice(helper.indexOf('.GLOBAL _dv_exp2;'), helper.indexOf('// R0 = the unsigned 16-bit'));
  const pcmMemory = 0x20110000;
  const tracks = pcmMemory + 0x1000;
  const tapes = tracks + 0x1000;
  const input = pcmMemory + 0x3000;
  const left = input + 0x100;
  const right = left + 0x100;
  const lead = right + 0x100;
  const second = lead + 0x100;
  const rx = pcmMemory + 0x4000;
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
${exp2}`,
        base: 0xb9a000,
      },
    ],
    externs: { _rtm_tracks: tracks, _rtm_tapes: tapes },
    returnSw: ret,
  });
  const owner = 3;
  const supportingTrack = 9;
  const row = tracks / 4 + owner * 16;
  const helperRow = tracks / 4 + supportingTrack * 16;
  const length = 64;
  const ownerSlot = 9 + owner;

  async function pool(R4: number, R8 = 0) {
    const result = await rig.call('_relay_pool', { R4, R8, R12: 0 }, { readRegs: ['R0'] });
    expect(result.halt).toBe('breakpoint');
    expect(result.regs.R0! >>> 0).not.toBe(0xffffffff);
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
  async function render(state: number, status = 2) {
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
    async function publish(marker: number) {
      const packet = [mask, marker, high, mask ^ marker ^ high ^ 0x22dca71b];
      packet.forEach((word, i) => rig.poke((rx + 0x854) / 4 + i, word));
      expect((await rig.call('_tk_pool_receive', { I3: rx })).halt).toBe('breakpoint');
    }
    await publish(0);
    for (const track of [owner, supportingTrack]) {
      const lookup = await rig.call('_tk_pool_head_owner', { R1: track }, { readRegs: ['R0'] });
      expect(lookup.regs.R0! >>> 0).toBe(0xffffffff); // A stock pool cannot authorize a RELAY Sound.
    }
    await publish(low);

    await note(lead, owner, 60);
    await note(second, supportingTrack, 72);
    expect(await render(lead)).toEqual(expected(0, 1));
    expect(await render(second)).toEqual(expected(0, 2));
    expect(await rig.peek(row + 1, 1)).toEqual([21]);
    expect(await rig.peek(helperRow + 1, 1)).toEqual([51]);

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
    expect(await render(lead, 3)).toEqual([Array(32).fill(0), Array(32).fill(0)]);
    expect(await render(second, 3)).toEqual([Array(32).fill(0), Array(32).fill(0)]);

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
    expect(await render(second)).toEqual([Array(32).fill(0), Array(32).fill(0)]);
    expect(await backing(blankRoot)).toEqual([0, 1]);

    rig.poke(row + 11, 1);
    await note(second, supportingTrack, 60);
    expect(await rig.peek(row + 11, 1)).toEqual([1]);
    await note(lead, owner, 60);
    expect(await rig.peek(row + 11, 1)).toEqual([0]);
  } finally {
    await rig.close();
  }
}, 90_000);
