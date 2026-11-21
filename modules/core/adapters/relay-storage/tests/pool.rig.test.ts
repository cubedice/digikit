import { readFileSync } from 'node:fs';
import path from 'node:path';
import { expect, it } from 'vitest';
import { RawRig } from '@digi/sharc-rig-kit';

const src = path.resolve(import.meta.dirname, '../src/dsp');
const contract = path.resolve(import.meta.dirname, '../../../../../sdk/dsp/contract');
const header = readFileSync(path.join(contract, 'dk_relay.inc'), 'utf8');
const op = Object.fromEntries([...header.matchAll(/^#define RLY_(\w+) (\d+)$/gm)].map(m => [m[1], Number(m[2])]));
const main = op.TRACK_MAIN!;
const alternate = op.TRACK_T1!;
const undo = op.TRACK_UNDO!;
const page = 16384;
const capacity = 511 * page;

async function open() {
  const rig = await RawRig.open({
    objects: [...['storage.s', 'pool.s'].map((name, i) => ({
      text: readFileSync(path.join(src, name), 'utf8').replaceAll('\r\n', '\n'), includes: [contract], base: 0xb90000 + 0x2000 * i,
    })), { text: `.SECTION/PM seg_pmco;
.GLOBAL _sample;
_sample: I3 = R4;
R0 = DM(0,I3);
RTS;`, base: 0xb98000 }],
    returnSw: 0xb9f100,
  });
  async function call(name: string, arg = 0, frame = 0) {
    const r = await rig.call('_relay_pool', { R4: op[name]!, R8: arg, R12: frame, M6: 1 }, { readRegs: ['R0', 'R1'] });
    expect(r.halt).toBe('breakpoint');
    return [r.regs.R0! >>> 0, r.regs.R1! >>> 0] as const;
  }
  async function result(name: string, arg = 0, frame = 0) {
    return (await call(name, arg, frame))[0];
  }
  async function fill(frames: number, tag: number) {
    let done = 0;
    while (done < frames) {
      const [address, span] = await call('ACQUIRE');
      expect(address).not.toBe(0xffffffff);
      const count = Math.min(frames - done, span);
      expect(count).toBeGreaterThan(0);
      rig.poke(address / 4, tag + done);
      rig.poke(address / 4 + count - 1, tag + done + count - 1);
      done += count;
      expect(await result('PUBLISH', count)).toBe(done);
    }
  }
  expect(await result('BEGIN', 16)).toBe(0xffffffff);
  expect(await result('RESET')).toBe(0);
  return { rig, call, result, fill };
}

it('fits a 60-second main and alternate, refuses a third, and recovers pinned scattered pages', async () => {
  const { rig, call, result, fill } = await open();
  const frames = 60 * 48000;
  const rounded = Math.ceil(frames / page) * page;
  try {
    for (const slot of [main, alternate]) {
      expect(await result('BEGIN', frames)).toBe(0);
      await fill(frames, slot === main ? 0 : 4000);
      expect(await result('COMMIT', slot)).toBe(0);
    }
    expect(await result('AVAILABLE')).toBe(capacity - 2 * rounded);
    expect(await result('BEGIN', frames)).toBe(0xffffffff);
    expect(await result('PIN', alternate)).toBe(0);
    expect(await result('CLEAR', alternate)).toBe(0);
    expect(await result('BEGIN', frames)).toBe(0xffffffff);
    const [tail, span] = await call('READ_SPAN', op.PINNED, frames - 1);
    expect(span).toBe(1);
    expect(await rig.peek(tail / 4)).toEqual([4000 + frames - 1]);
    expect(await result('UNPIN')).toBe(0);
    expect(await result('BEGIN', frames)).toBe(0);
    await fill(frames, 3000);
    expect(await result('FINISH_DUB', main)).toBe(0);
    expect(await result('SWAP', main, undo)).toBe(0);
    const [address] = await call('READ_SPAN', main, page);
    expect(await rig.peek(address / 4)).toEqual([page]);
    await result('CLEAR', main);
    await result('CLEAR', undo);
    expect(await result('AVAILABLE')).toBe(capacity);
  } finally {
    await rig.close();
  }
}, 120000);

it('bounds write spans and fixed takes, and returns the pages from canceled work', async () => {
  const { rig, call, result } = await open();
  try {
    expect(await result('BEGIN', 0)).toBe(0);
    expect(await result('RETARGET', 2)).toBe(0);
    expect((await call('ACQUIRE'))[1]).toBe(2);
    expect(await result('PUBLISH', 3)).toBe(0xffffffff);
    expect(await result('READ_SPAN', 8)).toBe(0xffffffff);
    expect(await result('ABORT')).toBe(0);
    expect(await result('AVAILABLE')).toBe(capacity);
    await result('BEGIN', 1);
    expect(await result('APPEND', 0xabcd1234)).toBe(1);
    expect(await result('APPEND', 0)).toBe(0xffffffff);
    expect(await result('REWRITE', 0xabcd2345, 0)).toBe(0);
    for (let slot = 0; slot < op.PINNED!; ++slot) {
      expect(await result('COMMIT', slot)).toBe(0xffffffff);
      expect(await result('SWAP', main, slot)).toBe(0xffffffff);
    }
    expect(await result('COMMIT', main)).toBe(0);
    const [p] = await call('READ_SPAN', main);
    expect(p).toBe(0x99a2b9c8 + 65536);
    const sample = await rig.call('_sample', { R4: p }, { readRegs: ['R0'] });
    expect(sample.regs.R0! >>> 0).toBe(0xabcd2345);
    expect(await result('READ_SPAN', main, 1)).toBe(0xffffffff);
  } finally {
    await rig.close();
  }
});

it('fills the shared pool without changing another track or publishing partial takes', async () => {
  const { rig, call, result, fill } = await open();
  try {
    expect(await result('BEGIN', 3)).toBe(0);
    await fill(3, 500);
    expect(await result('COMMIT', main + 1)).toBe(0);
    expect(await result('BEGIN', 0)).toBe(0);
    await fill(capacity - page, 1000);
    expect(await result('AVAILABLE')).toBe(0);
    expect(await result('LENGTH', main)).toBe(0);
    expect(await result('LENGTH', op.WRITER)).toBe(capacity - page);
    expect(await result('COMMIT', main)).toBe(0);
    expect(await result('BEGIN', 1)).toBe(0xffffffff);
    const [p] = await call('READ_SPAN', main + 1, 2);
    expect(await rig.peek(p / 4)).toEqual([502]);
    expect(await result('CLEAR', main)).toBe(0);
    expect(await result('AVAILABLE')).toBe(capacity - page);
  } finally {
    await rig.close();
  }
}, 120000);

it('shares saved tape and preserves untouched groups and undo across scattered pages', async () => {
  const { rig, call, result } = await open();
  const frames = page + 259;
  async function sample(slot: number, frame: number) {
    const [address] = await call('READ_SPAN', slot, frame);
    expect(address).not.toBe(0xffffffff);
    const value = await rig.call('_sample', {R4: address}, {readRegs: ['R0']});
    return value.regs.R0! >>> 0;
  }
  try {
    expect(await result('BEGIN', frames)).toBe(0);
    expect(await result('TAPE', -1 >>> 0)).toBe(0);
    expect(await result('REWRITE', 0x76543210, page + 258)).toBe(0);
    expect(await result('COMMIT', 9)).toBe(0);
    expect(await result('SHARE', 10, 9)).toBe(0);
    expect(await result('AVAILABLE')).toBe(capacity - 2 * page);
    expect(await result('BEGIN', frames)).toBe(0);
    expect(await result('TAPE', 10)).toBe(0);
    expect(await result('REWRITE', 0x11223344, 8193)).toBe(0);
    expect(await result('FINISH_DUB', 10)).toBe(0);
    expect(await sample(10, page + 258)).toBe(0x76543210);
    expect(await sample(10, 8193)).toBe(0x11223344);
    expect(await sample(9, 8193)).toBe(0);
    expect(await sample(58, 8193)).toBe(0);
    for (let i=0; i<Math.ceil(frames/256); ++i) await result('TIDY',10);
    for (const slot of [9,58,10]) expect(await result('CLEAR',slot)).toBe(0);
    expect(await result('AVAILABLE')).toBe(capacity);
  } finally { await rig.close(); }
}, 90000);
