import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect, it } from 'vitest';
import { RawRig, w2f } from '@digi/sharc-rig-kit';

it('renders separate stereo tracks at the recorder phase and retires a cleared track', async () => {
  const root = path.resolve(import.meta.dirname, '../../../..');
  const headers = path.join(root, 'sdk/dsp/contract');
  const header = readFileSync(path.join(headers, 'dk_relay.inc'), 'utf8');
  const op = Object.fromEntries([...header.matchAll(/^#define RLY_(\w+) (\d+)$/gm)].map(m => [m[1], Number(m[2])]));
  const sources = ['modules/core/adapters/relay-storage/src/dsp/storage.s',
    'modules/core/adapters/relay-storage/src/dsp/pool.s', 'modules/machines/relay/src/dsp/voice.s'];
  const tracks = 0x20111000;
  const ret = 0xb9f100;
  const rig = await RawRig.open({ objects: sources.map((source, i) => ({
    text: readFileSync(path.join(root, source), 'utf8').replaceAll('\r\n', '\n'),
    includes: [headers], base: 0xb90000 + i * 0x2000,
  })), externs: { _rtm_tracks: tracks, _rtm_tapes: tracks + 0x1000 }, returnSw: ret });
  const a = 0x20110000, b = a + 256, input = b + 256, left = input + 128, right = left + 256, params = right + 256;
  async function pool(name: string, arg = 0) {
    const result = await rig.call('_relay_pool', { R4: op[name]!, R8: arg, R12: 0 }, { readRegs: ['R0'] });
    expect(result.halt).toBe('breakpoint');
    expect(result.regs.R0! >>> 0).not.toBe(0xffffffff);
  }
  async function note(state: number, bank: number) {
    rig.poke(input / 4 + 4, bank);
    const r = await rig.call('_relay_note_on', { R4: state, R8: input, R12: params, M13: 0, I13: ret }, {readRegs:['R0','R1','R2','I0'],reads:[[state/4+5,1],[input/4+4,1]]});
    expect(r.halt,JSON.stringify(r)).toBe('breakpoint');
  }
  async function render(state: number, done = false) {
    const r = await rig.call('_relay_render', { R4: state, R8: input, M13: 0, I13: ret, M6: 1 },
      { reads: [[left / 4, 32], [right / 4, 32]], readRegs: ['R0'] });
    expect(r.halt).toBe('breakpoint');
    expect(r.regs.R0).toBe(done ? 3 : 2);
    return r.reads.map(words => words.map(word => w2f(word ?? 0) * 32768));
  }
  try {
    for(let i=0;i<256;++i) rig.poke(tracks/4+i,0);
    for(let t=0;t<16;++t)rig.poke((tracks+0x1000)/4+t*128,0);
    await pool('RESET');
    for (const [bank, length] of [[0, 41], [2, 5]]) {
      await pool('BEGIN');
      for (let i = 0; i < length!; i++) {
        const value = i + bank! * 100 + 1;
        await pool('APPEND', ((value << 16) | (-value & 65535)) >>> 0);
      }
      await pool('COMMIT', 9 + bank!);
      rig.poke(tracks / 4 + bank! * 16, length!);
      rig.poke(tracks / 4 + bank! * 16 + 2, 9 + bank!);
    }
    rig.poke(input / 4 + 3, left);
    rig.poke(input / 4 + 5, right);
    await note(a, 0);
    await note(b, 2);
    const expected = (from: number, length: number, bias: number) => [1, -1].map(sign =>
      Array.from({length: 32}, (_, n) => sign * (bias + 1 + (from + n) % length)));
    expect(await render(a)).toEqual(expected(0, 41, 0));
    expect(await render(b)).toEqual(expected(0, 5, 200));
    rig.poke(tracks / 4 + 1, 32);
    rig.poke(tracks / 4 + 2 * 16 + 1, 32 % 5);
    expect(await render(a)).toEqual(expected(32, 41, 0));
    await note(a, 0);
    expect(await render(a)).toEqual(expected(0, 41, 0));
    await pool('CLEAR', 9);
    expect(await render(a, true)).toEqual([Array(32).fill(0), Array(32).fill(0)]);
    expect(await render(b)).toEqual(expected(32, 5, 200));
  } finally {
    await rig.close();
  }
}, 90000);
