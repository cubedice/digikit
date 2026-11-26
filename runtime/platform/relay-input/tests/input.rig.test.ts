import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect, it } from 'vitest';
import { RawRig } from '@digi/sharc-rig-kit';

const tap = readFileSync(path.resolve(import.meta.dirname, '../src/dsp/input.s'), 'utf8').replaceAll('\r\n', '\n');
const data = 0x20110000;
const tail = 0xb9f100;
const master = data + 0x400;
const externs = {
  _relay_input_tail: tail,
  _rtm_capture: 0,
  _tk_pool_fold: 0,
  _relay_q31_converter: tail,
  _relay_master_float: master,
};

it.each([0, 0x200000])('preserves the native mixer registers with MODE1=%i', async mode => {
  const rig = await RawRig.open({
    objects: [{ text: tap, base: 0xb90000 }],
    externs,
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

it.each([0, 0x200000])('copies the completed master block before the converter with MODE1=%i', async (mode) => {
  const rig = await RawRig.open({
    objects: [{ text: tap, base: 0xb90000 }],
    externs,
    returnSw: tail,
  });
  try {
    const samples = Array.from({ length: 64 }, (_, i) => 0x3f000000 + 0x8000 * i + (i >= 32 ? 0x80000000 : 0));
    const source = [0xdeadbeef, ...samples, 0x12345678];
    source.forEach((word, i) => rig.poke(master / 4 - 1 + i, word));
    const regs = {
      R0: 123456,
      S0: 654321,
      R4: 0x1234,
      R8: 0x5678,
      I0: data + 4,
      I1: data + 8,
      B0: data,
      B1: data,
      L0: 8,
      L1: 16,
      M6: 3,
      LCNTR: 17,
      ASTATX: 0x83,
      ASTATY: 0x0a,
      MODE1: mode,
    };
    const result = await rig.call('_relay_main_tap', regs, {
      readRegs: Object.keys(regs),
      reads: [
        [rig.data._relay_main! / 4, 64],
        [master / 4 - 1, source.length],
        [rig.data._relay_external! / 4, 64],
      ],
    });
    expect(result.halt).toBe('breakpoint');
    expect(result.final_pc).toBe(tail);
    expect(result.regs).toMatchObject(regs);
    expect(result.reads[0]).toEqual(samples);
    expect(result.reads[1]).toEqual(source);
    expect(result.reads[2]).toEqual(Array(64).fill(0));
  } finally {
    await rig.close();
  }
});
