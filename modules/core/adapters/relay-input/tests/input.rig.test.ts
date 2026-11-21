import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect, it } from 'vitest';
import { RawRig } from '@digi/sharc-rig-kit';

const tap = readFileSync(path.resolve(import.meta.dirname, '../src/dsp/input.s'), 'utf8').replaceAll('\r\n', '\n');
const data = 0x20110000;
const tail = 0xb9f100;

it.each([0, 0x200000])('preserves the native mixer registers with MODE1=%i', async mode => {
  const rig = await RawRig.open({
    objects: [{ text: tap, base: 0xb90000 }],
    externs: { _relay_input_tail: tail, _rtm_capture: 0 },
    returnSw: tail,
  });
  try {
    rig.poke(data / 4, data + 128);
    rig.poke(data / 4 + 1, data + 256);
    const samples = Array.from({ length: 64 }, (_, i) => 1600 + i);
    samples.forEach((word, i) => rig.poke((data + 128) / 4 + i, word));
    const regs = { R0: 123456, S0: 654321, R8: data, MODE1: mode, I0: data + 4, I1: data + 8, L0: 0, L1: 0, M6: 3 };
    const result = await rig.call('_relay_input_tap', regs, {
      readRegs: Object.keys(regs), reads: [[rig.data._relay_external! / 4, 64]],
    });
    expect(result.halt).toBe('breakpoint');
    expect(result.regs).toMatchObject(regs);
    expect(result.reads[0]).toEqual(samples);
  } finally {
    await rig.close();
  }
});
